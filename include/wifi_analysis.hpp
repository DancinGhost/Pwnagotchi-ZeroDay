#pragma once

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "domain.hpp"
#include "fp.hpp"

namespace zd {

// Pure string sanitizer: strips non-printable characters so TFT bitmap fonts render cleanly.
inline std::string sanitizePrintable(const std::string& raw) {
    std::string cleaned;
    cleaned.reserve(raw.size());
    for (unsigned char ch : raw) {
        if (ch >= 32u && ch <= 126u) {
            cleaned.push_back(static_cast<char>(ch));
        }
    }
    // Trim trailing spaces
    while (!cleaned.empty() && cleaned.back() == ' ') {
        cleaned.pop_back();
    }
    return cleaned.empty() ? std::string("<hidden>") : cleaned;
}

// Pure normalization of a single raw WiFi scan entry.
inline AccessPoint sanitizeAccessPoint(const AccessPoint& raw) {
    const int32_t normalizedChannel = (raw.channel >= 1 && raw.channel <= 13) ? raw.channel : 0;
    const int32_t clampedRssi = fp::clampVal<int32_t>(raw.rssi, -100, 0);
    return AccessPoint{
        sanitizePrintable(raw.ssid),
        clampedRssi,
        normalizedChannel,
        raw.secure
    };
}

// Pure comparator: strongest RSSI first, deterministic tie-breaker by SSID then channel.
inline bool isStrongerAp(const AccessPoint& a, const AccessPoint& b) {
    if (a.rssi != b.rssi) {
        return a.rssi > b.rssi;
    }
    if (a.ssid != b.ssid) {
        return a.ssid < b.ssid;
    }
    return a.channel < b.channel;
}

// Pure fold that deduplicates repeated mesh BSSIDs from a sorted AP list,
// keeping the strongest signal for each named SSID (and per-channel for <hidden>).
inline std::vector<AccessPoint> deduplicateSortedAps(const std::vector<AccessPoint>& sortedAps) {
    return fp::foldl(
        sortedAps,
        std::vector<AccessPoint>{},
        [](std::vector<AccessPoint> acc, const AccessPoint& candidate) {
            const bool alreadyPresent = fp::countIf(acc, [&](const AccessPoint& existing) {
                if (candidate.ssid == "<hidden>") {
                    return existing.ssid == "<hidden>" && existing.channel == candidate.channel;
                }
                return existing.ssid == candidate.ssid;
            }) > 0;

            if (!alreadyPresent) {
                acc.push_back(candidate);
            }
            return acc;
        });
}

// Pure fold computing per-channel AP counts (channels 1..13, plus bucket 0 for 5GHz/other).
inline ChannelHistogram buildChannelHistogram(const std::vector<AccessPoint>& aps) {
    return fp::foldl(
        aps,
        ChannelHistogram{},
        [](ChannelHistogram hist, const AccessPoint& ap) {
            const std::size_t bucket =
                (ap.channel >= 1 && ap.channel <= 13) ? static_cast<std::size_t>(ap.channel) : 0u;
            if (hist.counts[bucket] < 999u) {
                hist.counts[bucket] = static_cast<uint16_t>(hist.counts[bucket] + 1u);
            }
            return hist;
        });
}

// Pure fold finding which 2.4 GHz channel (1..13) has the most APs.
inline int32_t findBusiestChannel(const ChannelHistogram& hist) {
    constexpr std::array<int32_t, 13> kChannels = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    struct Best {
        int32_t channel;
        uint16_t count;
    };
    const Best best = fp::foldl(
        kChannels,
        Best{0, 0},
        [&](Best current, int32_t ch) {
            const uint16_t c = hist.counts[static_cast<std::size_t>(ch)];
            return (c > current.count) ? Best{ch, c} : current;
        });
    return best.channel;
}

// Pure mapping from RSSI dBm to 0..4 visual signal strength bars.
constexpr int rssiToBars(int32_t rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -78) return 2;
    if (rssi >= -88) return 1;
    return 0;
}

// Pure functional pipeline transforming raw scan records into an immutable ScanSummary.
inline ScanSummary summarizeScan(const std::vector<AccessPoint>& rawNetworks,
                                 std::size_t maxTop = 6) {
    const std::vector<AccessPoint> sanitized = fp::pipe(
        rawNetworks,
        fp::mapped(sanitizeAccessPoint),
        fp::sortedBy(isStrongerAp));

    const std::size_t total = sanitized.size();
    const std::size_t secureCount = fp::countIf(sanitized, [](const AccessPoint& ap) {
        return ap.secure;
    });
    const ChannelHistogram histogram = buildChannelHistogram(sanitized);
    const int32_t busiestCh = findBusiestChannel(histogram);

    const std::vector<AccessPoint> topUnique = fp::pipe(
        sanitized,
        deduplicateSortedAps,
        fp::taken(maxTop));

    const int32_t bestRssi = sanitized.empty() ? -100 : sanitized.front().rssi;
    const std::string bestSsid = sanitized.empty() ? std::string{} : sanitized.front().ssid;

    return ScanSummary{
        total,
        secureCount,
        total - secureCount,
        bestRssi,
        busiestCh,
        bestSsid,
        topUnique,
        histogram
    };
}

}  // namespace zd
