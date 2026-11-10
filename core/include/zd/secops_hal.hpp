#pragma once

// Portable adapter contracts for the Linux desktop security backend + a thin coordinator.
//
// Zero OS / socket / libpcap / libnl headers live here. Concrete implementations in
// platforms/linux/backend/ provide either native (libpcap / libnl / nl80211) or
// CLI-tool (iw / airodump-ng / aireplay-ng / airmon-ng) realizations of these interfaces.
// The `SecSession` coordinator is the imperative boundary that owns `SecState`, applies
// the pure reducer, and enforces the authorization gate on every transmit.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "secops.hpp"

namespace zd {
namespace sec {

// A single decoded 802.11 frame of interest, produced by a monitor-mode capture source.
enum class FrameKind : uint8_t {
    Beacon = 0,       // or probe-response: carries SSID/enc/channel
    ProbeRequest,
    Data,
    Deauth,
    Disassoc,
    Eapol,            // 4-way handshake message
    Pmkid             // PMKID present (e.g. first EAPOL / RSN IE)
};

struct CapturedFrame {
    uint32_t nowMs = 0;
    FrameKind kind = FrameKind::Beacon;
    std::string bssid;
    std::string source;
    std::string dest;
    std::string ssid;             // for Beacon / ProbeRequest
    int32_t channel = 0;
    int32_t rssi = -100;
    Encryption enc = Encryption::Unknown;
};

// --- Adapter interfaces --------------------------------------------------------------

// Puts an interface into (and out of) monitor mode and controls channel selection.
class IMonitorControl {
public:
    virtual ~IMonitorControl() = default;
    // Enable monitor mode. Returns the monitor interface name (e.g. "wlan0mon") or empty on failure.
    virtual std::string enableMonitor(const std::string& iface) = 0;
    virtual void disableMonitor() = 0;
    virtual bool setChannel(int channel) = 0;
    virtual bool isMonitor() const = 0;
    virtual std::string monitorIface() const = 0;
};

// Non-blocking monitor-mode frame source (libpcap or airodump-ng CSV/pcap tail).
class ICaptureSource {
public:
    virtual ~ICaptureSource() = default;
    virtual bool start(const std::string& monitorIface) = 0;
    // Drain any frames decoded since the last poll. Never blocks the UI loop.
    virtual std::vector<CapturedFrame> poll() = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
};

// Result of a transmit request, so the shell can surface success/refusal uniformly.
struct InjectResult {
    AuthDecision decision = AuthDecision::BlockedUnconfirmed;
    bool transmitted = false;    // true only if the backend actually sent frames
    std::string detail;          // human-readable backend message / audit note
};

// Frame injector (deauthentication). Implementations MUST assume the caller has already
// cleared the authorization gate; `SecSession::requestDeauth` is the only intended caller.
class IInjector {
public:
    virtual ~IInjector() = default;
    virtual bool available() const = 0;
    // Send `count` deauth frames against `bssid`. `client` empty => broadcast to all stations.
    virtual InjectResult deauth(const std::string& bssid, const std::string& client,
                                uint32_t count) = 0;
};

// Optional audit sink: a durable record of every transmit decision (allowed or blocked).
class IAuditLog {
public:
    virtual ~IAuditLog() = default;
    virtual void record(const std::string& line) = 0;
};

// --- Coordinator ---------------------------------------------------------------------

class SecSession {
public:
    const SecState& state() const { return state_; }
    SecState& mutableState() { return state_; }

    Authorization& authorization() { return auth_; }
    const Authorization& authorization() const { return auth_; }

    // Feed one observed frame into the pure engine.
    void ingest(const CapturedFrame& f) {
        switch (f.kind) {
            case FrameKind::Beacon:
                dispatch(ApObserved{f.nowMs, f.bssid, f.ssid, f.channel, f.rssi, f.enc});
                break;
            case FrameKind::ProbeRequest:
                // A probe request reveals a station; attribute to BSSID if known (directed
                // probe) and always record the probed SSID as a station-level signal, since
                // broadcast/unassociated probes (no BSSID) still leak a preferred network.
                if (!f.bssid.empty()) {
                    dispatch(ClientObserved{f.nowMs, f.bssid, f.source});
                }
                if (!f.source.empty()) {
                    dispatch(ProbeRequestObserved{f.nowMs, f.source, f.ssid});
                }
                break;
            case FrameKind::Data:
                dispatch(DataFrameObserved{f.nowMs, f.bssid});
                if (!f.source.empty() && !isBroadcastMac(f.source)) {
                    dispatch(ClientObserved{f.nowMs, f.bssid, f.source});
                }
                break;
            case FrameKind::Deauth:
            case FrameKind::Disassoc:
                dispatch(DeauthObserved{f.nowMs, f.bssid, f.source, f.dest});
                break;
            case FrameKind::Eapol:
                dispatch(HandshakeObserved{f.nowMs, f.bssid});
                break;
            case FrameKind::Pmkid:
                dispatch(PmkidObserved{f.nowMs, f.bssid});
                break;
        }
    }

    void dispatch(const SecEvent& ev) { state_ = reduce(state_, ev); }

    // Fast, synchronous half of the transmit chokepoint: resolves the authorization
    // decision and records the attempt (state + audit log) immediately. Never calls the
    // injector, so it never blocks on backend I/O — safe to call right before handing the
    // actual (potentially slow) `injector.deauth()` off to a worker thread.
    AuthDecision beginDeauth(const std::string& bssid, const std::string& client, uint32_t count,
                             uint32_t nowMs, IAuditLog* audit = nullptr) {
        const AuthDecision decision = authorize(auth_, bssid);
        dispatch(InjectionAttempt{nowMs, bssid, client, count, decision});
        if (audit != nullptr) {
            if (decision != AuthDecision::Allowed) {
                audit->record("DEAUTH BLOCKED bssid=" + normalizeMac(bssid) +
                              " reason=" + std::string(authDecisionLabel(decision)));
            } else {
                audit->record("DEAUTH ALLOWED bssid=" + normalizeMac(bssid) +
                              " client=" + (client.empty() ? std::string("broadcast") : client) +
                              " count=" + std::to_string(count));
            }
        }
        return decision;
    }

    // THE transmit chokepoint for synchronous callers (fine for one-shot, non-interactive
    // CLI use): gates via beginDeauth(), then transmits immediately on the calling thread.
    // A front-end driving a responsive UI loop should call beginDeauth() directly and run
    // the injector off-thread instead (see AppController::requestDeauth in the Linux
    // desktop shell), since injector.deauth() can block for a while.
    InjectResult requestDeauth(IInjector& injector, const std::string& bssid,
                               const std::string& client, uint32_t count, uint32_t nowMs,
                               IAuditLog* audit = nullptr) {
        const AuthDecision decision = beginDeauth(bssid, client, count, nowMs, audit);
        InjectResult res{};
        res.decision = decision;
        if (decision != AuthDecision::Allowed) {
            res.transmitted = false;
            res.detail = authDecisionLabel(decision);
            return res;
        }
        res = injector.deauth(bssid, client, count);
        res.decision = decision;  // preserve the gate decision regardless of backend detail
        return res;
    }

private:
    SecState state_{};
    Authorization auth_{};
};

}  // namespace sec
}  // namespace zd
