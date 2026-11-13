#pragma once

// Portable, zero-dependency security-operations engine (Functional Core).
//
// This header carries ZERO OS, socket, libpcap, libnl, or SDK headers. It is the pure,
// host-testable brain of the Linux desktop build's offensive + defensive features. The
// imperative shell (platforms/linux/*) feeds observed 802.11 activity in as `SecEvent`s
// and renders the resulting immutable `SecState`; it never embeds policy here.
//
// Responsibilities:
//   - Model observed access points / clients and offensive capture progress
//     (WPA handshakes, PMKIDs) as immutable values.
//   - Defensive intrusion monitors expressed as pure reducers:
//       * deauthentication / disassociation flood detection (sliding window),
//       * evil-twin detection (one SSID advertised by multiple BSSIDs),
//       * open-network downgrade detection (encrypted SSID reappears open),
//       * "mana"/karma beacon detection (one BSSID advertising many SSIDs).
//   - A strict AUTHORIZATION gate: no transmit (deauth injection) is ever permitted
//     unless the operator has both confirmed authorization AND explicitly allow-listed
//     the target BSSID. This is the single chokepoint the shell must consult before TX.
//
// Design mirrors pet_logic.hpp: `reduce(SecState, SecEvent) -> SecState`, never mutating
// its input. Everything here is deterministic and unit-tested in tests/core/test_secops.cpp.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace zd {
namespace sec {

// --- Value types ---------------------------------------------------------------------

enum class Encryption : uint8_t { Unknown = 0, Open, Wep, Wpa, Wpa2, Wpa3 };

inline const char* encryptionLabel(Encryption e) {
    switch (e) {
        case Encryption::Open:    return "OPEN";
        case Encryption::Wep:     return "WEP";
        case Encryption::Wpa:     return "WPA";
        case Encryption::Wpa2:    return "WPA2";
        case Encryption::Wpa3:    return "WPA3";
        case Encryption::Unknown: break;
    }
    return "??";
}

// Pure MAC normalizer: lower-case, colon-separated "aa:bb:cc:dd:ee:ff".
// Accepts "AABBCCDDEEFF", "aa-bb-...", mixed case, surrounding spaces. Non-hex chars are
// dropped; a string without exactly 12 hex nibbles is returned trimmed+lowered as-is so
// callers can still compare/display it (defensive: never throws).
inline std::string normalizeMac(const std::string& raw) {
    std::string hex;
    hex.reserve(12);
    for (unsigned char c : raw) {
        if (std::isxdigit(c)) {
            hex.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    if (hex.size() != 12u) {
        std::string lowered;
        lowered.reserve(raw.size());
        for (unsigned char c : raw) {
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                lowered.push_back(static_cast<char>(std::tolower(c)));
            }
        }
        return lowered;
    }
    std::string out;
    out.reserve(17);
    for (std::size_t i = 0; i < 12u; i += 2u) {
        if (i != 0u) {
            out.push_back(':');
        }
        out.push_back(hex[i]);
        out.push_back(hex[i + 1u]);
    }
    return out;
}

inline bool isBroadcastMac(const std::string& mac) {
    return mac == "ff:ff:ff:ff:ff:ff" || mac.empty();
}

// True if `raw` contains exactly 12 hex digits once separators/whitespace are stripped,
// i.e. normalizeMac(raw) would produce a well-formed "aa:bb:cc:dd:ee:ff" address rather than
// just returning the (lowered/trimmed) input verbatim. Used to reject malformed BSSID/client
// values at input boundaries (CLI args, GUI text fields) before they reach a subprocess argv.
inline bool isValidMac(const std::string& raw) {
    std::size_t hexCount = 0;
    for (unsigned char c : raw) {
        if (std::isxdigit(c)) {
            ++hexCount;
        } else if (c != ':' && c != '-' && c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            return false;  // stray character that isn't hex or a separator (e.g. a stray flag)
        }
    }
    return hexCount == 12u;
}

// An access point observed over the air or via an active scan.
struct WifiTarget {
    std::string bssid;              // normalized
    std::string ssid;              // may be "" for hidden networks
    int32_t channel = 0;
    int32_t rssi = -100;
    Encryption enc = Encryption::Unknown;
    uint32_t beacons = 0;
    uint32_t dataFrames = 0;
    uint32_t deauthsSeen = 0;      // deauth/disassoc frames referencing this BSSID
    uint32_t clientCount = 0;
    bool handshake = false;        // WPA 4-way handshake captured
    bool pmkid = false;            // PMKID captured
    bool wps = false;              // WPS (Wi-Fi Protected Setup) IE observed - common attack surface
    uint32_t firstSeenMs = 0;
    uint32_t lastSeenMs = 0;
};

// --- Defensive alerts ----------------------------------------------------------------

enum class Severity : uint8_t { Info = 0, Warn, Critical };

enum class AlertKind : uint8_t {
    DeauthFlood = 0,   // sustained deauth/disassoc burst against a BSSID (possible attack/jam)
    EvilTwin,          // same SSID advertised by a second BSSID
    OpenDowngrade,     // a previously-encrypted SSID reappeared as Open (rogue/MITM bait)
    KarmaBeacon,       // one BSSID advertising an abnormal number of SSIDs (mana/karma AP)
    HandshakeCaptured, // offensive progress marker
    PmkidCaptured,     // offensive progress marker
    TxBlocked          // an injection was refused by the authorization gate
};

inline const char* alertKindLabel(AlertKind k) {
    switch (k) {
        case AlertKind::DeauthFlood:       return "DEAUTH-FLOOD";
        case AlertKind::EvilTwin:          return "EVIL-TWIN";
        case AlertKind::OpenDowngrade:     return "OPEN-DOWNGRADE";
        case AlertKind::KarmaBeacon:       return "KARMA-BEACON";
        case AlertKind::HandshakeCaptured: return "HANDSHAKE";
        case AlertKind::PmkidCaptured:     return "PMKID";
        case AlertKind::TxBlocked:         return "TX-BLOCKED";
    }
    return "ALERT";
}

struct Alert {
    uint32_t nowMs = 0;
    Severity severity = Severity::Info;
    AlertKind kind = AlertKind::DeauthFlood;
    std::string bssid;
    std::string ssid;
    std::string message;
};

// --- Authorization gate --------------------------------------------------------------
//
// The one place that decides whether a transmit (deauth injection) is permitted. Pure &
// total. The shell MUST call `authorize()` and only proceed on `Allowed`.

enum class AuthDecision : uint8_t {
    Allowed = 0,
    BlockedUnconfirmed,    // operator has not asserted authorization
    BlockedNotAllowlisted  // target BSSID not on the explicit allow-list
};

struct Authorization {
    bool operatorConfirmed = false;      // set only by explicit operator action (flag / dialog)
    std::vector<std::string> allowedBssids;  // normalized BSSIDs the operator owns / may test
    bool allowAll = false;               // "*" wildcard; STILL requires operatorConfirmed

    void allow(const std::string& bssid) {
        const std::string n = normalizeMac(bssid);
        if (std::find(allowedBssids.begin(), allowedBssids.end(), n) == allowedBssids.end()) {
            allowedBssids.push_back(n);
        }
    }
};

inline AuthDecision authorize(const Authorization& auth, const std::string& bssid) {
    if (!auth.operatorConfirmed) {
        return AuthDecision::BlockedUnconfirmed;
    }
    // Reject broadcast or empty BSSID to prevent unintended transmission or packet corruption
    const std::string n = normalizeMac(bssid);
    if (isBroadcastMac(n)) {
        return AuthDecision::BlockedNotAllowlisted;
    }
    if (auth.allowAll) {
        return AuthDecision::Allowed;
    }
    if (std::find(auth.allowedBssids.begin(), auth.allowedBssids.end(), n) !=
        auth.allowedBssids.end()) {
        return AuthDecision::Allowed;
    }
    return AuthDecision::BlockedNotAllowlisted;
}

inline const char* authDecisionLabel(AuthDecision d) {
    switch (d) {
        case AuthDecision::Allowed:               return "ALLOWED";
        case AuthDecision::BlockedUnconfirmed:    return "BLOCKED: authorization not confirmed";
        case AuthDecision::BlockedNotAllowlisted: return "BLOCKED: BSSID not on allow-list";
    }
    return "BLOCKED";
}

// --- Events fed from the imperative shell --------------------------------------------

struct ApObserved {      // from beacon/probe-response or active scan
    uint32_t nowMs;
    std::string bssid;
    std::string ssid;
    int32_t channel;
    int32_t rssi;
    Encryption enc;
    bool wps = false;
};

struct ClientObserved {  // a station associated with / probing a BSSID
    uint32_t nowMs;
    std::string bssid;
    std::string client;
};

struct DataFrameObserved {
    uint32_t nowMs;
    std::string bssid;
};

struct DeauthObserved {  // management deauth or disassociation frame
    uint32_t nowMs;
    std::string bssid;
    std::string source;
    std::string dest;
};

struct HandshakeObserved {
    uint32_t nowMs;
    std::string bssid;
};

struct PmkidObserved {
    uint32_t nowMs;
    std::string bssid;
};

// A client probing for a (possibly not-yet-associated) network by SSID. Probe requests
// leak a station's preferred-network list even over broadcast/unassociated probes, which
// is why this is tracked independently of ClientObserved (which requires a known BSSID).
struct ProbeRequestObserved {
    uint32_t nowMs;
    std::string client;
    std::string ssid;   // SSID being probed for; may be "" for a wildcard/broadcast probe
};

// Recorded whenever the shell asks the gate about a transmit; keeps the decision in the
// state/alert stream so the UI and audit log stay consistent. `allowed` mirrors the gate.
struct InjectionAttempt {
    uint32_t nowMs;
    std::string bssid;
    std::string client;   // target station, or "" for broadcast
    uint32_t count;       // frames requested
    AuthDecision decision;
};

using SecEvent = std::variant<
    ApObserved,
    ClientObserved,
    DataFrameObserved,
    DeauthObserved,
    HandshakeObserved,
    PmkidObserved,
    ProbeRequestObserved,
    InjectionAttempt>;

// --- Immutable aggregate state -------------------------------------------------------

// Tuning constants for the defensive monitors.
constexpr uint32_t kDeauthWindowMs = 10000u;   // sliding window for flood detection
constexpr std::size_t kDeauthFloodThreshold = 8u;  // frames in window => flood
constexpr uint32_t kAlertCooldownMs = 15000u;  // per-(kind,bssid) alert de-dupe window
constexpr std::size_t kKarmaSsidThreshold = 6u;    // distinct SSIDs per BSSID => karma
constexpr std::size_t kMaxAlerts = 200u;       // ring cap on retained alerts
constexpr std::size_t kMaxProbedSsidsPerStation = 16u;  // cap per-station probed-SSID list
constexpr std::size_t kMaxStations = 500u;     // ring cap; evicts least-recently-seen station

// Per-BSSID sliding window of recent deauth timestamps (defensive flood detector).
struct DeauthTracker {
    std::string bssid;
    std::vector<uint32_t> recentMs;
    uint32_t lastFloodAlertMs = 0;
};

// Per-BSSID set of advertised SSIDs (evil-twin reverse lookup is by SSID below).
struct BssidSsids {
    std::string bssid;
    std::vector<std::string> ssids;
    uint32_t lastKarmaAlertMs = 0;
};

// A client station observed either associating with a known BSSID or sending probe
// requests (which can happen before association, or to networks it never joins).
// Tracked separately from WifiTarget to surface a station's probed-SSID history - a
// passive privacy/recon signal (phones leak their preferred-network list on every probe).
struct Station {
    std::string mac;                       // normalized
    std::vector<std::string> probedSsids;  // distinct SSIDs this station has probed for
    std::string lastBssid;                 // most recent BSSID observed associating with it
    uint32_t probeCount = 0;
    uint32_t firstSeenMs = 0;
    uint32_t lastSeenMs = 0;
};

struct SecState {
    std::vector<WifiTarget> targets;
    std::vector<DeauthTracker> deauthTrackers;
    std::vector<BssidSsids> bssidSsids;
    std::vector<Station> stations;
    std::vector<Alert> alerts;         // most-recent appended last, capped at kMaxAlerts
    uint32_t totalDeauths = 0;
    uint32_t totalHandshakes = 0;
    uint32_t totalPmkids = 0;
    uint32_t blockedInjections = 0;
    uint32_t allowedInjections = 0;
};

// --- Pure helpers (exposed for tests & shell convenience) ----------------------------

inline WifiTarget* findTarget(SecState& s, const std::string& bssid) {
    for (WifiTarget& t : s.targets) {
        if (t.bssid == bssid) {
            return &t;
        }
    }
    return nullptr;
}

inline const WifiTarget* findTarget(const SecState& s, const std::string& bssid) {
    for (const WifiTarget& t : s.targets) {
        if (t.bssid == bssid) {
            return &t;
        }
    }
    return nullptr;
}

inline Station* findStation(SecState& s, const std::string& mac) {
    for (Station& st : s.stations) {
        if (st.mac == mac) {
            return &st;
        }
    }
    return nullptr;
}

inline const Station* findStation(const SecState& s, const std::string& mac) {
    for (const Station& st : s.stations) {
        if (st.mac == mac) {
            return &st;
        }
    }
    return nullptr;
}

namespace detail {

inline void pushAlert(SecState& s, Alert a) {
    s.alerts.push_back(std::move(a));
    if (s.alerts.size() > kMaxAlerts) {
        s.alerts.erase(s.alerts.begin(),
                       s.alerts.begin() +
                           static_cast<std::ptrdiff_t>(s.alerts.size() - kMaxAlerts));
    }
}

inline DeauthTracker& trackerFor(SecState& s, const std::string& bssid) {
    for (DeauthTracker& d : s.deauthTrackers) {
        if (d.bssid == bssid) {
            return d;
        }
    }
    s.deauthTrackers.push_back(DeauthTracker{bssid, {}, 0u});
    return s.deauthTrackers.back();
}

inline BssidSsids& ssidsFor(SecState& s, const std::string& bssid) {
    for (BssidSsids& b : s.bssidSsids) {
        if (b.bssid == bssid) {
            return b;
        }
    }
    s.bssidSsids.push_back(BssidSsids{bssid, {}, 0u});
    return s.bssidSsids.back();
}

// Upserts a station, evicting the least-recently-seen one first if at the ring cap.
inline Station& stationFor(SecState& s, const std::string& mac) {
    for (Station& st : s.stations) {
        if (st.mac == mac) {
            return st;
        }
    }
    if (s.stations.size() >= kMaxStations) {
        std::size_t oldestIdx = 0u;
        for (std::size_t i = 1u; i < s.stations.size(); ++i) {
            if (s.stations[i].lastSeenMs < s.stations[oldestIdx].lastSeenMs) {
                oldestIdx = i;
            }
        }
        s.stations.erase(s.stations.begin() + static_cast<std::ptrdiff_t>(oldestIdx));
    }
    s.stations.push_back(Station{});
    s.stations.back().mac = mac;
    return s.stations.back();
}

// Count how many distinct BSSIDs currently advertise `ssid` (evil-twin signal).
inline std::size_t bssidsAdvertising(const SecState& s, const std::string& ssid) {
    std::size_t n = 0;
    for (const WifiTarget& t : s.targets) {
        if (!t.ssid.empty() && t.ssid == ssid) {
            ++n;
        }
    }
    return n;
}

}  // namespace detail

// --- The reducer ---------------------------------------------------------------------

inline SecState reduce(const SecState& state, const SecEvent& event) {
    SecState s = state;  // value copy; pure transformation

    std::visit(
        [&](const auto& ev) {
            using E = std::decay_t<decltype(ev)>;

            if constexpr (std::is_same_v<E, ApObserved>) {
                const std::string bssid = normalizeMac(ev.bssid);
                if (bssid.empty()) {
                    return;
                }
                WifiTarget* t = findTarget(s, bssid);
                const bool isNew = (t == nullptr);
                if (isNew) {
                    s.targets.push_back(WifiTarget{});
                    t = &s.targets.back();
                    t->bssid = bssid;
                    t->firstSeenMs = ev.nowMs;
                }
                const std::string prevSsid = t->ssid;
                t->ssid = ev.ssid;
                t->channel = ev.channel;
                t->rssi = ev.rssi;
                t->enc = ev.enc;
                t->wps = ev.wps;
                t->beacons += 1u;
                t->lastSeenMs = ev.nowMs;

                // Evil-twin: a second BSSID now advertises this SSID.
                if (!ev.ssid.empty() && detail::bssidsAdvertising(s, ev.ssid) >= 2u &&
                    (isNew || prevSsid != ev.ssid)) {
                    detail::pushAlert(s, Alert{ev.nowMs, Severity::Warn, AlertKind::EvilTwin,
                                               bssid, ev.ssid,
                                               "SSID '" + ev.ssid +
                                                   "' advertised by multiple BSSIDs"});
                }
                // Open-downgrade: SSID that was encrypted now seen Open on a (new) BSSID.
                if (ev.enc == Encryption::Open && !ev.ssid.empty()) {
                    for (const WifiTarget& other : s.targets) {
                        if (other.bssid != bssid && other.ssid == ev.ssid &&
                            other.enc != Encryption::Open &&
                            other.enc != Encryption::Unknown) {
                            detail::pushAlert(
                                s, Alert{ev.nowMs, Severity::Critical, AlertKind::OpenDowngrade,
                                         bssid, ev.ssid,
                                         "Open AP impersonating encrypted SSID '" + ev.ssid +
                                             "'"});
                            break;
                        }
                    }
                }
                // Karma/mana: this BSSID advertising many distinct SSIDs.
                if (!ev.ssid.empty()) {
                    BssidSsids& bs = detail::ssidsFor(s, bssid);
                    if (std::find(bs.ssids.begin(), bs.ssids.end(), ev.ssid) == bs.ssids.end()) {
                        bs.ssids.push_back(ev.ssid);
                    }
                    if (bs.ssids.size() >= kKarmaSsidThreshold &&
                        (bs.lastKarmaAlertMs == 0u ||
                         ev.nowMs - bs.lastKarmaAlertMs >= kAlertCooldownMs)) {
                        bs.lastKarmaAlertMs = ev.nowMs;
                        detail::pushAlert(
                            s, Alert{ev.nowMs, Severity::Warn, AlertKind::KarmaBeacon, bssid, ev.ssid,
                                     "BSSID advertising " + std::to_string(bs.ssids.size()) +
                                         " SSIDs (possible karma/mana AP)"});
                    }
                }

            } else if constexpr (std::is_same_v<E, ClientObserved>) {
                const std::string bssid = normalizeMac(ev.bssid);
                WifiTarget* t = findTarget(s, bssid);
                if (t != nullptr) {
                    t->clientCount += 1u;
                    t->lastSeenMs = ev.nowMs;
                }
                const std::string client = normalizeMac(ev.client);
                if (!client.empty() && !isBroadcastMac(client)) {
                    Station& st = detail::stationFor(s, client);
                    if (st.firstSeenMs == 0u) {
                        st.firstSeenMs = ev.nowMs;
                    }
                    st.lastBssid = bssid;
                    st.lastSeenMs = ev.nowMs;
                }

            } else if constexpr (std::is_same_v<E, ProbeRequestObserved>) {
                const std::string client = normalizeMac(ev.client);
                if (client.empty() || isBroadcastMac(client)) {
                    return;
                }
                Station& st = detail::stationFor(s, client);
                if (st.firstSeenMs == 0u) {
                    st.firstSeenMs = ev.nowMs;
                }
                st.lastSeenMs = ev.nowMs;
                st.probeCount += 1u;
                if (!ev.ssid.empty() &&
                    std::find(st.probedSsids.begin(), st.probedSsids.end(), ev.ssid) ==
                        st.probedSsids.end() &&
                    st.probedSsids.size() < kMaxProbedSsidsPerStation) {
                    st.probedSsids.push_back(ev.ssid);
                }

            } else if constexpr (std::is_same_v<E, DataFrameObserved>) {
                WifiTarget* t = findTarget(s, normalizeMac(ev.bssid));
                if (t != nullptr) {
                    t->dataFrames += 1u;
                    t->lastSeenMs = ev.nowMs;
                }

            } else if constexpr (std::is_same_v<E, DeauthObserved>) {
                const std::string bssid = normalizeMac(ev.bssid);
                s.totalDeauths += 1u;
                if (WifiTarget* t = findTarget(s, bssid)) {
                    t->deauthsSeen += 1u;
                    t->lastSeenMs = ev.nowMs;
                }
                DeauthTracker& dt = detail::trackerFor(s, bssid);
                // Drop timestamps outside the sliding window.
                dt.recentMs.erase(
                    std::remove_if(dt.recentMs.begin(), dt.recentMs.end(),
                                   [&](uint32_t t) { return ev.nowMs - t > kDeauthWindowMs; }),
                    dt.recentMs.end());
                dt.recentMs.push_back(ev.nowMs);
                if (dt.recentMs.size() >= kDeauthFloodThreshold &&
                    (dt.lastFloodAlertMs == 0u ||
                     ev.nowMs - dt.lastFloodAlertMs >= kAlertCooldownMs)) {
                    dt.lastFloodAlertMs = ev.nowMs;
                    const WifiTarget* t = findTarget(s, bssid);
                    detail::pushAlert(
                        s, Alert{ev.nowMs, Severity::Critical, AlertKind::DeauthFlood, bssid,
                                 t != nullptr ? t->ssid : std::string{},
                                 std::to_string(dt.recentMs.size()) +
                                     " deauth/disassoc frames in " +
                                     std::to_string(kDeauthWindowMs / 1000u) + "s"});
                }

            } else if constexpr (std::is_same_v<E, HandshakeObserved>) {
                const std::string bssid = normalizeMac(ev.bssid);
                WifiTarget* t = findTarget(s, bssid);
                const bool wasNew = (t == nullptr) || !t->handshake;
                if (t == nullptr) {
                    s.targets.push_back(WifiTarget{});
                    t = &s.targets.back();
                    t->bssid = bssid;
                    t->firstSeenMs = ev.nowMs;
                }
                t->handshake = true;
                t->lastSeenMs = ev.nowMs;
                if (wasNew) {
                    s.totalHandshakes += 1u;
                    detail::pushAlert(s, Alert{ev.nowMs, Severity::Info, AlertKind::HandshakeCaptured,
                                               bssid, t->ssid, "WPA handshake captured"});
                }

            } else if constexpr (std::is_same_v<E, PmkidObserved>) {
                const std::string bssid = normalizeMac(ev.bssid);
                WifiTarget* t = findTarget(s, bssid);
                const bool wasNew = (t == nullptr) || !t->pmkid;
                if (t == nullptr) {
                    s.targets.push_back(WifiTarget{});
                    t = &s.targets.back();
                    t->bssid = bssid;
                    t->firstSeenMs = ev.nowMs;
                }
                t->pmkid = true;
                t->lastSeenMs = ev.nowMs;
                if (wasNew) {
                    s.totalPmkids += 1u;
                    detail::pushAlert(s, Alert{ev.nowMs, Severity::Info, AlertKind::PmkidCaptured,
                                               bssid, t->ssid, "PMKID captured"});
                }

            } else if constexpr (std::is_same_v<E, InjectionAttempt>) {
                const std::string bssid = normalizeMac(ev.bssid);
                if (ev.decision == AuthDecision::Allowed) {
                    s.allowedInjections += 1u;
                } else {
                    s.blockedInjections += 1u;
                    const WifiTarget* t = findTarget(s, bssid);
                    detail::pushAlert(
                        s, Alert{ev.nowMs, Severity::Warn, AlertKind::TxBlocked, bssid,
                                 t != nullptr ? t->ssid : std::string{},
                                 std::string("Injection refused - ") +
                                     authDecisionLabel(ev.decision)});
                }
            }
        },
        event);

    return s;
}

// Sort targets strongest-signal-first for display (pure, returns a copy).
inline std::vector<WifiTarget> targetsByStrength(const SecState& s) {
    std::vector<WifiTarget> out = s.targets;
    std::sort(out.begin(), out.end(), [](const WifiTarget& a, const WifiTarget& b) {
        if (a.rssi != b.rssi) {
            return a.rssi > b.rssi;
        }
        return a.bssid < b.bssid;
    });
    return out;
}

// --- Channel band classification & distribution (recon helper) ----------------------
//
// Unlike the portable pet UI's 2.4GHz-only ChannelHistogram (shared with ESP32 hardware
// that is physically incapable of 5GHz), the Linux security engine already records every
// target's real channel number untouched, so it can report the full band picture.

enum class Band : uint8_t { Unknown = 0, Band24GHz, Band5GHz };

inline const char* bandLabel(Band b) {
    switch (b) {
        case Band::Band24GHz: return "2.4GHz";
        case Band::Band5GHz:  return "5GHz";
        case Band::Unknown:   break;
    }
    return "?";
}

// Classify a channel number into its band by numeric range. Covers every current 2.4GHz
// (1-14) and 5GHz U-NII (36-177) channel; anything else (6GHz, out-of-range) is Unknown
// rather than guessed, since 6GHz channel numbers alias 2.4GHz ones without extra band info.
constexpr Band classifyChannel(int32_t channel) {
    if (channel >= 1 && channel <= 14) {
        return Band::Band24GHz;
    }
    if (channel >= 36 && channel <= 177) {
        return Band::Band5GHz;
    }
    return Band::Unknown;
}

struct ChannelCount {
    int32_t channel;
    Band band;
    uint32_t count;
};

// Pure fold: how many currently-tracked targets sit on each channel, across every band the
// Linux backend can observe. Sorted by channel ascending.
inline std::vector<ChannelCount> channelDistribution(const SecState& s) {
    std::vector<ChannelCount> out;
    for (const WifiTarget& t : s.targets) {
        bool found = false;
        for (ChannelCount& c : out) {
            if (c.channel == t.channel) {
                ++c.count;
                found = true;
                break;
            }
        }
        if (!found) {
            out.push_back(ChannelCount{t.channel, classifyChannel(t.channel), 1u});
        }
    }
    std::sort(out.begin(), out.end(),
              [](const ChannelCount& a, const ChannelCount& b) { return a.channel < b.channel; });
    return out;
}

// Sort stations by probing activity (most active first) for display (pure, returns a copy).
inline std::vector<Station> stationsByActivity(const SecState& s) {
    std::vector<Station> out = s.stations;
    std::sort(out.begin(), out.end(), [](const Station& a, const Station& b) {
        if (a.probeCount != b.probeCount) {
            return a.probeCount > b.probeCount;
        }
        return a.mac < b.mac;
    });
    return out;
}

// --- Ambient "pet mood" signal from defensive alerts ---------------------------------
//
// The portable Mood enum (domain.hpp/pet_logic.hpp) is shared by every platform, including
// hardware that has never heard of secops. Rather than reach into that cross-platform
// surface for a signal only the Linux desktop currently produces, this is a small,
// independent, purely-derived enum a host UI (GUI pet widget, TUI status line) can use to
// make the pet visibly react to live threats.

enum class SecMood : uint8_t { Calm = 0, Watchful, Alarmed };

inline const char* secMoodLabel(SecMood m) {
    switch (m) {
        case SecMood::Calm:     return "CALM";
        case SecMood::Watchful: return "WATCHFUL";
        case SecMood::Alarmed:  return "ALARMED";
    }
    return "CALM";
}

// Pure: looks at alerts within `recentMs` of `nowMs`, most-recent-first. Any Critical
// alert in that window => Alarmed; any Warn => Watchful; otherwise Calm. Alerts are
// append-ordered oldest-to-newest, so the reverse scan can stop at the first one that
// falls outside the window.
inline SecMood deriveSecMood(const SecState& s, uint32_t nowMs, uint32_t recentMs = 30000u) {
    bool anyWarn = false;
    for (auto it = s.alerts.rbegin(); it != s.alerts.rend(); ++it) {
        if (nowMs - it->nowMs > recentMs) {
            break;
        }
        if (it->severity == Severity::Critical) {
            return SecMood::Alarmed;
        }
        if (it->severity == Severity::Warn) {
            anyWarn = true;
        }
    }
    return anyWarn ? SecMood::Watchful : SecMood::Calm;
}

// --- Session record & replay: pure text wire format for SecEvent --------------------
//
// One SecEvent per line, '|'-delimited. String fields come straight off the air (SSIDs,
// BSSIDs, alert text) and are not trusted, so they are escaped rather than assumed free of
// the delimiter. File I/O is the imperative shell's job (fopen/fwrite); this is only the
// pure encode/decode + replay-through-the-reducer primitives, so it stays host-testable
// without any backend. A session recorded this way replays deterministically through
// `reduce()` to reproduce the exact same SecState, for review, demos, or regression tests.

namespace detail {

inline std::string escapeRecordField(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (c == '\\' || c == '|') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

inline std::string unescapeRecordField(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char c = raw[i];
        if (c == '\\' && i + 1 < raw.size()) {
            const char n = raw[++i];
            if (n == 'n') {
                out.push_back('\n');
            } else if (n == 'r') {
                out.push_back('\r');
            } else {
                out.push_back(n);
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// Splits on unescaped '|', leaving escape sequences intact for unescapeRecordField().
inline std::vector<std::string> splitRecordFields(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\\' && i + 1 < line.size()) {
            cur.push_back(c);
            cur.push_back(line[++i]);
        } else if (c == '|') {
            fields.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    fields.push_back(cur);
    return fields;
}

inline std::string joinRecordFields(const std::vector<std::string>& fields) {
    std::string out;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i != 0u) {
            out.push_back('|');
        }
        out += escapeRecordField(fields[i]);
    }
    return out;
}

inline uint32_t parseU32(const std::string& s) {
    try {
        return static_cast<uint32_t>(std::stoul(s));
    } catch (...) {
        return 0u;
    }
}

inline int32_t parseI32(const std::string& s) {
    try {
        return static_cast<int32_t>(std::stol(s));
    } catch (...) {
        return 0;
    }
}

}  // namespace detail

inline std::string encodeSecEvent(const SecEvent& event) {
    return std::visit(
        [](const auto& ev) -> std::string {
            using E = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<E, ApObserved>) {
                return detail::joinRecordFields(
                    {"AP", std::to_string(ev.nowMs), ev.bssid, ev.ssid,
                     std::to_string(ev.channel), std::to_string(ev.rssi),
                     std::to_string(static_cast<int>(ev.enc)), ev.wps ? "1" : "0"});
            } else if constexpr (std::is_same_v<E, ClientObserved>) {
                return detail::joinRecordFields(
                    {"CLIENT", std::to_string(ev.nowMs), ev.bssid, ev.client});
            } else if constexpr (std::is_same_v<E, DataFrameObserved>) {
                return detail::joinRecordFields({"DATA", std::to_string(ev.nowMs), ev.bssid});
            } else if constexpr (std::is_same_v<E, DeauthObserved>) {
                return detail::joinRecordFields(
                    {"DEAUTH", std::to_string(ev.nowMs), ev.bssid, ev.source, ev.dest});
            } else if constexpr (std::is_same_v<E, HandshakeObserved>) {
                return detail::joinRecordFields({"HANDSHAKE", std::to_string(ev.nowMs), ev.bssid});
            } else if constexpr (std::is_same_v<E, PmkidObserved>) {
                return detail::joinRecordFields({"PMKID", std::to_string(ev.nowMs), ev.bssid});
            } else if constexpr (std::is_same_v<E, ProbeRequestObserved>) {
                return detail::joinRecordFields(
                    {"PROBE", std::to_string(ev.nowMs), ev.client, ev.ssid});
            } else if constexpr (std::is_same_v<E, InjectionAttempt>) {
                return detail::joinRecordFields(
                    {"INJECT", std::to_string(ev.nowMs), ev.bssid, ev.client,
                     std::to_string(ev.count), std::to_string(static_cast<int>(ev.decision))});
            }
            return std::string{};
        },
        event);
}

inline std::optional<SecEvent> decodeSecEvent(const std::string& line) {
    if (line.empty()) {
        return std::nullopt;
    }
    std::vector<std::string> f;
    for (const std::string& raw : detail::splitRecordFields(line)) {
        f.push_back(detail::unescapeRecordField(raw));
    }
    if (f.empty()) {
        return std::nullopt;
    }
    const std::string& tag = f[0];
    using detail::parseI32;
    using detail::parseU32;

    if (tag == "AP" && f.size() == 8u) {
        return SecEvent{ApObserved{parseU32(f[1]), f[2], f[3], parseI32(f[4]), parseI32(f[5]),
                                   static_cast<Encryption>(parseI32(f[6])), f[7] == "1"}};
    }
    if (tag == "CLIENT" && f.size() == 4u) {
        return SecEvent{ClientObserved{parseU32(f[1]), f[2], f[3]}};
    }
    if (tag == "DATA" && f.size() == 3u) {
        return SecEvent{DataFrameObserved{parseU32(f[1]), f[2]}};
    }
    if (tag == "DEAUTH" && f.size() == 5u) {
        return SecEvent{DeauthObserved{parseU32(f[1]), f[2], f[3], f[4]}};
    }
    if (tag == "HANDSHAKE" && f.size() == 3u) {
        return SecEvent{HandshakeObserved{parseU32(f[1]), f[2]}};
    }
    if (tag == "PMKID" && f.size() == 3u) {
        return SecEvent{PmkidObserved{parseU32(f[1]), f[2]}};
    }
    if (tag == "PROBE" && f.size() == 4u) {
        return SecEvent{ProbeRequestObserved{parseU32(f[1]), f[2], f[3]}};
    }
    if (tag == "INJECT" && f.size() == 6u) {
        return SecEvent{InjectionAttempt{parseU32(f[1]), f[2], f[3], parseU32(f[4]),
                                         static_cast<AuthDecision>(parseI32(f[5]))}};
    }
    return std::nullopt;
}

// Encodes a whole session to one line per event, in order (ready to join with "\n" and
// write to a file - the imperative shell's job).
inline std::vector<std::string> encodeSecEvents(const std::vector<SecEvent>& events) {
    std::vector<std::string> lines;
    lines.reserve(events.size());
    for (const SecEvent& ev : events) {
        lines.push_back(encodeSecEvent(ev));
    }
    return lines;
}

// Decodes a recorded session back into events, silently skipping any line that fails to
// parse (e.g. a truncated last line) rather than aborting the whole replay.
inline std::vector<SecEvent> decodeSecEvents(const std::vector<std::string>& lines) {
    std::vector<SecEvent> events;
    events.reserve(lines.size());
    for (const std::string& line : lines) {
        if (std::optional<SecEvent> ev = decodeSecEvent(line)) {
            events.push_back(*ev);
        }
    }
    return events;
}

// Pure fold: replays a recorded event log through `reduce()` from an empty SecState,
// reproducing the exact SecState a live session would have reached - a deterministic
// replay is the whole point of recording through a pure reducer in the first place.
inline SecState replaySecEvents(const std::vector<SecEvent>& events) {
    SecState s{};
    for (const SecEvent& ev : events) {
        s = reduce(s, ev);
    }
    return s;
}

}  // namespace sec
}  // namespace zd
