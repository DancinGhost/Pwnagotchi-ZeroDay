#pragma once

// Pure, host-testable 802.11 / radiotap frame decoder.
//
// Turns a raw monitor-mode capture buffer (radiotap header + 802.11 frame) into a portable
// `zd::sec::CapturedFrame`. No libpcap/socket here, so tests can feed byte arrays directly.
// The libpcap IO lives in native_backend.hpp and simply calls decodeRadiotapFrame().

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include "zd/secops.hpp"
#include "zd/secops_hal.hpp"

namespace zd {
namespace linux_backend {

inline std::string macToString(const uint8_t* p) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", p[0], p[1], p[2], p[3], p[4],
                  p[5]);
    return std::string(buf);
}

struct RadiotapInfo {
    uint16_t length = 0;
    int32_t signalDbm = -100;
    bool hasSignal = false;
};

// Parse a radiotap header: returns its byte length and, when present, the dBm signal.
inline RadiotapInfo parseRadiotap(const uint8_t* data, std::size_t len) {
    RadiotapInfo info;
    if (len < 8 || data[0] != 0x00) {  // version 0
        return info;
    }
    const uint16_t rtLen = static_cast<uint16_t>(data[2] | (data[3] << 8));
    if (rtLen < 8 || rtLen > len) {
        return info;
    }
    info.length = rtLen;

    const uint32_t firstPresent = static_cast<uint32_t>(
        data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24));
    // Skip any extended present-flag words (bit 31 chains another word).
    std::size_t off = 8;
    uint32_t present = firstPresent;
    while (present & 0x80000000u) {
        if (off + 4 > rtLen) {
            return info;
        }
        present = static_cast<uint32_t>(data[off] | (data[off + 1] << 8) | (data[off + 2] << 16) |
                                        (data[off + 3] << 24));
        off += 4;
    }

    auto align = [&](std::size_t a) { off = (off + (a - 1)) & ~(a - 1); };
    const uint32_t pbit = firstPresent;
    if (pbit & (1u << 0)) { align(8); off += 8; }   // TSFT
    if (pbit & (1u << 1)) { off += 1; }             // Flags
    if (pbit & (1u << 2)) { off += 1; }             // Rate
    if (pbit & (1u << 3)) { align(2); off += 4; }   // Channel
    if (pbit & (1u << 4)) { off += 2; }             // FHSS
    if (pbit & (1u << 5)) {                         // dBm antenna signal
        if (off < rtLen) {
            info.signalDbm = static_cast<int8_t>(data[off]);
            info.hasSignal = true;
        }
    }
    return info;
}

// Map an RSN information element (tag 48 body) to WPA2/WPA3 by inspecting the AKM suites.
inline sec::Encryption encryptionFromRsn(const uint8_t* ie, std::size_t len) {
    // version(2) group(4) pairwiseCount(2) pairwise(4*n) akmCount(2) akm(4*m)
    if (len < 8) {
        return sec::Encryption::Wpa2;
    }
    std::size_t off = 2 + 4;  // skip version + group cipher
    if (off + 2 > len) {
        return sec::Encryption::Wpa2;
    }
    const uint16_t pairwiseCount = static_cast<uint16_t>(ie[off] | (ie[off + 1] << 8));
    off += 2 + static_cast<std::size_t>(pairwiseCount) * 4u;
    if (off + 2 > len) {
        return sec::Encryption::Wpa2;
    }
    const uint16_t akmCount = static_cast<uint16_t>(ie[off] | (ie[off + 1] << 8));
    off += 2;
    for (uint16_t i = 0; i < akmCount && off + 4 <= len; ++i, off += 4) {
        // AKM suite: OUI(3) + type(1). Type 8 = SAE (WPA3), 18 = OWE.
        const uint8_t suiteType = ie[off + 3];
        if (suiteType == 8 || suiteType == 18) {
            return sec::Encryption::Wpa3;
        }
    }
    return sec::Encryption::Wpa2;
}

// Parse the tagged parameters of a beacon/probe body: fills SSID, channel, encryption.
inline void parseTaggedParams(const uint8_t* tags, std::size_t tlen, bool privacyBit,
                              sec::CapturedFrame& out) {
    bool rsn = false;
    sec::Encryption rsnEnc = sec::Encryption::Wpa2;
    bool wpaVendor = false;
    std::size_t i = 0;
    while (i + 2 <= tlen) {
        const uint8_t id = tags[i];
        const uint8_t tl = tags[i + 1];
        const std::size_t body = i + 2;
        if (body + tl > tlen) {
            break;
        }
        if (id == 0) {  // SSID
            std::string ssid;
            for (std::size_t k = 0; k < tl; ++k) {
                const unsigned char c = tags[body + k];
                if (c >= 32u && c <= 126u) {
                    ssid.push_back(static_cast<char>(c));
                }
            }
            out.ssid = ssid;  // empty => hidden
        } else if (id == 3 && tl >= 1) {  // DS parameter: current channel
            out.channel = tags[body];
        } else if (id == 48) {  // RSN
            rsn = true;
            rsnEnc = encryptionFromRsn(tags + body, tl);
        } else if (id == 221 && tl >= 4) {  // vendor-specific
            if (tags[body] == 0x00 && tags[body + 1] == 0x50 && tags[body + 2] == 0xF2 &&
                tags[body + 3] == 0x01) {
                wpaVendor = true;  // Microsoft WPA1 IE
            }
        }
        i = body + tl;
    }
    if (rsn) {
        out.enc = rsnEnc;
    } else if (wpaVendor) {
        out.enc = sec::Encryption::Wpa;
    } else if (privacyBit) {
        out.enc = sec::Encryption::Wep;
    } else {
        out.enc = sec::Encryption::Open;
    }
}

// True if a data frame carries an EAPOL (0x888E) payload after the MAC+LLC/SNAP headers.
inline bool looksLikeEapol(const uint8_t* f, std::size_t len) {
    const uint8_t subtype = static_cast<uint8_t>((f[0] >> 4) & 0xF);
    std::size_t hdr = 24;
    if ((subtype & 0x08) != 0) {  // QoS data: +2 byte QoS control
        hdr += 2;
    }
    // LLC/SNAP: AA AA 03 00 00 00 <ethertype>
    if (hdr + 8 > len) {
        return false;
    }
    const uint8_t* llc = f + hdr;
    if (llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03) {
        const uint16_t ethertype = static_cast<uint16_t>((llc[6] << 8) | llc[7]);
        return ethertype == 0x888E;
    }
    return false;
}

// Heuristic: an EAPOL M1 carrying an RSN PMKID KDE (OUI 00-0F-AC, data type 0x04).
inline bool carriesPmkid(const uint8_t* f, std::size_t len) {
    for (std::size_t i = 0; i + 4 <= len; ++i) {
        if (f[i] == 0x00 && f[i + 1] == 0x0F && f[i + 2] == 0xAC && f[i + 3] == 0x04) {
            return true;
        }
    }
    return false;
}

// Decode one 802.11 frame (no radiotap). `rssi`/`nowMs` supplied by caller.
inline std::optional<sec::CapturedFrame> decodeDot11(const uint8_t* f, std::size_t len,
                                                     uint32_t nowMs, int32_t rssi) {
    if (len < 24) {
        return std::nullopt;
    }
    const uint8_t fc0 = f[0];
    const uint8_t type = static_cast<uint8_t>((fc0 >> 2) & 0x3);
    const uint8_t subtype = static_cast<uint8_t>((fc0 >> 4) & 0xF);
    const uint8_t fc1 = f[1];
    const bool toDs = (fc1 & 0x01) != 0;
    const bool fromDs = (fc1 & 0x02) != 0;

    const uint8_t* addr1 = f + 4;
    const uint8_t* addr2 = f + 10;
    const uint8_t* addr3 = f + 16;

    sec::CapturedFrame out;
    out.nowMs = nowMs;
    out.rssi = rssi;

    if (type == 0) {  // management
        out.source = macToString(addr2);
        out.dest = macToString(addr1);
        out.bssid = macToString(addr3);
        if (subtype == 8 || subtype == 5) {  // Beacon / Probe Response
            out.kind = sec::FrameKind::Beacon;
            if (len >= 36) {
                const uint16_t caps = static_cast<uint16_t>(f[34] | (f[35] << 8));
                const bool privacy = (caps & 0x0010) != 0;
                parseTaggedParams(f + 36, len - 36, privacy, out);
            }
            return out;
        }
        if (subtype == 4) {  // Probe Request
            out.kind = sec::FrameKind::ProbeRequest;
            if (len > 24) {
                parseTaggedParams(f + 24, len - 24, false, out);
            }
            return out;
        }
        if (subtype == 12) {
            out.kind = sec::FrameKind::Deauth;
            return out;
        }
        if (subtype == 10) {
            out.kind = sec::FrameKind::Disassoc;
            return out;
        }
        return std::nullopt;
    }

    if (type == 2) {  // data
        if (!toDs && !fromDs) {
            out.bssid = macToString(addr3);
            out.source = macToString(addr2);
            out.dest = macToString(addr1);
        } else if (!toDs && fromDs) {
            out.bssid = macToString(addr2);
            out.dest = macToString(addr1);
            out.source = macToString(addr3);
        } else if (toDs && !fromDs) {
            out.bssid = macToString(addr1);
            out.source = macToString(addr2);
            out.dest = macToString(addr3);
        } else {
            out.bssid = macToString(addr1);
            out.source = macToString(addr2);
        }
        if (looksLikeEapol(f, len)) {
            out.kind = carriesPmkid(f, len) ? sec::FrameKind::Pmkid : sec::FrameKind::Eapol;
            return out;
        }
        out.kind = sec::FrameKind::Data;
        return out;
    }

    return std::nullopt;
}

// Full decode from a radiotap-prefixed capture buffer (what libpcap hands us in DLT_IEEE802_11_RADIO).
inline std::optional<sec::CapturedFrame> decodeRadiotapFrame(const uint8_t* buf, std::size_t len,
                                                             uint32_t nowMs) {
    const RadiotapInfo rt = parseRadiotap(buf, len);
    if (rt.length == 0 || rt.length >= len) {
        return std::nullopt;
    }
    const int32_t rssi = rt.hasSignal ? rt.signalDbm : -100;
    return decodeDot11(buf + rt.length, len - rt.length, nowMs, rssi);
}

}  // namespace linux_backend
}  // namespace zd
