#pragma once

// Pure, host-testable parsers for Linux WiFi-tool output.
//
// These contain NO subprocess or socket calls: they turn the textual output of `nmcli`,
// `iw dev scan`, and `airodump-ng`'s CSV dump into portable `zd` value types. Keeping the
// parsing pure means tests/core/test_linux_parsers.cpp can validate them against captured
// sample output with zero hardware or root. The thin IO adapters (cli_backend.hpp) only
// shell out and hand the resulting text to these functions.

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "zd/domain.hpp"
#include "zd/secops.hpp"
#include "zd/secops_hal.hpp"

namespace zd {
namespace linux_backend {

inline std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) {
        ++a;
    }
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) {
        --b;
    }
    return s.substr(a, b - a);
}

// Map a human security string ("WPA2", "WPA3 802.1X", "--", "CCMP", "OPN") to Encryption.
inline sec::Encryption encryptionFromText(const std::string& raw) {
    std::string up;
    up.reserve(raw.size());
    for (char c : raw) {
        up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (up.find("WPA3") != std::string::npos || up.find("SAE") != std::string::npos) {
        return sec::Encryption::Wpa3;
    }
    if (up.find("WPA2") != std::string::npos || up.find("RSN") != std::string::npos ||
        up.find("CCMP") != std::string::npos) {
        return sec::Encryption::Wpa2;
    }
    if (up.find("WPA") != std::string::npos) {
        return sec::Encryption::Wpa;
    }
    if (up.find("WEP") != std::string::npos) {
        return sec::Encryption::Wep;
    }
    const std::string t = trim(up);
    if (t.empty() || t == "--" || t.find("OPN") != std::string::npos ||
        t.find("OPEN") != std::string::npos || t.find("NONE") != std::string::npos) {
        return sec::Encryption::Open;
    }
    return sec::Encryption::Unknown;
}

// nmcli signal 0..100 -> approximate dBm (nmcli reports quality, not raw dBm).
inline int32_t nmcliSignalToDbm(int quality) {
    if (quality <= 0) {
        return -100;
    }
    if (quality >= 100) {
        return -30;
    }
    return static_cast<int32_t>(-100 + (quality * 70) / 100);
}

// Split a terminal-colon-escaped nmcli line ("-t" mode) honoring backslash escapes.
inline std::vector<std::string> splitNmcliFields(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\\' && i + 1 < line.size()) {
            cur.push_back(line[++i]);
        } else if (c == ':') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

// Parse `nmcli -t -f BSSID,SSID,CHAN,SECURITY,SIGNAL dev wifi list`.
// Returns both legacy AccessPoint rows (for IRadioScanner) and richer WifiTarget-ish data.
inline std::vector<AccessPoint> parseNmcliScan(const std::string& text) {
    std::vector<AccessPoint> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (trim(line).empty()) {
            continue;
        }
        const std::vector<std::string> f = splitNmcliFields(line);
        if (f.size() < 5) {
            continue;
        }
        // Fields: BSSID, SSID, CHAN, SECURITY, SIGNAL
        AccessPoint ap;
        ap.ssid = trim(f[1]);
        ap.channel = std::atoi(f[2].c_str());
        const sec::Encryption enc = encryptionFromText(f[3]);
        ap.secure = (enc != sec::Encryption::Open && enc != sec::Encryption::Unknown);
        ap.rssi = nmcliSignalToDbm(std::atoi(f[4].c_str()));
        out.push_back(ap);
    }
    return out;
}

// Richer variant retaining BSSID + encryption, used to feed the security engine.
struct ScannedAp {
    std::string bssid;
    std::string ssid;
    int32_t channel;
    int32_t rssi;
    sec::Encryption enc;
};

inline std::vector<ScannedAp> parseNmcliScanDetailed(const std::string& text) {
    std::vector<ScannedAp> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (trim(line).empty()) {
            continue;
        }
        const std::vector<std::string> f = splitNmcliFields(line);
        if (f.size() < 5) {
            continue;
        }
        ScannedAp ap;
        ap.bssid = sec::normalizeMac(trim(f[0]));
        ap.ssid = trim(f[1]);
        ap.channel = std::atoi(f[2].c_str());
        ap.enc = encryptionFromText(f[3]);
        ap.rssi = nmcliSignalToDbm(std::atoi(f[4].c_str()));
        if (!ap.bssid.empty()) {
            out.push_back(ap);
        }
    }
    return out;
}

// Parse the AP section of an airodump-ng "-w prefix --output-format csv" dump.
// airodump CSV has two comma-separated sections separated by a blank line:
//   Section 1 header: BSSID, First time seen, Last time seen, channel, Speed, Privacy,
//                     Cipher, Authentication, Power, # beacons, # IV, LAN IP, ID-length,
//                     ESSID, Key
//   Section 2 header: Station MAC, First time seen, Last time seen, Power, # packets,
//                     BSSID, Probed ESSIDs
// We emit ApObserved-style rows for section 1 and client rows for section 2.
struct AirodumpAp {
    std::string bssid;
    std::string ssid;
    int32_t channel;
    int32_t rssi;
    sec::Encryption enc;
    uint32_t beacons;
};

struct AirodumpStation {
    std::string mac;
    std::string bssid;   // associated BSSID, or "(not associated)"
};

struct AirodumpDump {
    std::vector<AirodumpAp> aps;
    std::vector<AirodumpStation> stations;
};

inline std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ',') {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(trim(cur));
    return out;
}

inline AirodumpDump parseAirodumpCsv(const std::string& text) {
    AirodumpDump dump;
    std::istringstream in(text);
    std::string line;
    bool inStations = false;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty()) {
            continue;
        }
        if (t.rfind("BSSID,", 0) == 0 || t.rfind("BSSID ,", 0) == 0) {
            inStations = false;
            continue;  // AP header
        }
        if (t.rfind("Station MAC", 0) == 0) {
            inStations = true;
            continue;  // station header
        }
        const std::vector<std::string> f = splitCsv(line);
        if (!inStations) {
            if (f.size() < 14) {
                continue;
            }
            AirodumpAp ap;
            ap.bssid = sec::normalizeMac(f[0]);
            if (ap.bssid.empty()) {
                continue;
            }
            ap.channel = std::atoi(f[3].c_str());
            // Privacy(5) + Cipher(6) + Authentication(7) describe encryption.
            ap.enc = encryptionFromText(f[5] + " " + f[6] + " " + f[7]);
            ap.rssi = std::atoi(f[8].c_str());
            ap.beacons = static_cast<uint32_t>(std::atoi(f[9].c_str()));
            ap.ssid = f[13];
            dump.aps.push_back(ap);
        } else {
            if (f.size() < 6) {
                continue;
            }
            AirodumpStation st;
            st.mac = sec::normalizeMac(f[0]);
            st.bssid = sec::normalizeMac(f[5]);
            if (!st.mac.empty()) {
                dump.stations.push_back(st);
            }
        }
    }
    return dump;
}

}  // namespace linux_backend
}  // namespace zd
