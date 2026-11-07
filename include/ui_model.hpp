#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "domain.hpp"
#include "fp.hpp"
#include "pet_logic.hpp"
#include "wifi_analysis.hpp"

namespace zd {

struct ScreenDimensions {
    int16_t width = 240;
    int16_t height = 135;
};

struct ColorPalette {
    uint16_t bg;
    uint16_t fg;
    uint16_t accent;
    uint16_t muted;
    uint16_t highlight;
};

// Pure constexpr 24-bit RGB -> 16-bit RGB565 conversion.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((static_cast<uint16_t>(r & 0xF8u)) << 8) |
                                 ((static_cast<uint16_t>(g & 0xFCu)) << 3) |
                                 (static_cast<uint16_t>(b) >> 3));
}

// Pure theme -> RGB565 palette mapping.
constexpr ColorPalette resolvePalette(ThemeId theme) {
    switch (theme) {
        case ThemeId::PaperInk:
            return ColorPalette{
                rgb565(242, 240, 230),  // Warm e-paper background
                rgb565(22, 24, 28),     // Crisp charcoal ink
                rgb565(48, 58, 92),     // Deep slate accent
                rgb565(155, 152, 142),  // Subtle divider gray
                rgb565(215, 95, 110)    // Warm cheek/signal highlight
            };
        case ThemeId::DarkInk:
            return ColorPalette{
                rgb565(10, 12, 16),
                rgb565(244, 246, 250),
                rgb565(165, 195, 255),
                rgb565(72, 78, 92),
                rgb565(255, 130, 150)
            };
        case ThemeId::ZeroDayCyan:
            return ColorPalette{
                rgb565(6, 12, 22),
                rgb565(0, 238, 255),
                rgb565(120, 255, 200),
                rgb565(30, 68, 96),
                rgb565(255, 90, 180)
            };
        case ThemeId::PhosphorGreen:
            return ColorPalette{
                rgb565(6, 12, 6),
                rgb565(56, 255, 96),
                rgb565(175, 255, 175),
                rgb565(26, 86, 36),
                rgb565(120, 255, 120)
            };
    }
    return ColorPalette{
        rgb565(242, 240, 230),
        rgb565(22, 24, 28),
        rgb565(48, 58, 92),
        rgb565(155, 152, 142),
        rgb565(215, 95, 110)
    };
}

// Pure zero-padded 2-digit number formatter.
inline std::string twoDigit(uint32_t val) {
    const uint32_t clamped = val % 100u;
    std::string out(2, '0');
    out[0] = static_cast<char>('0' + (clamped / 10u));
    out[1] = static_cast<char>('0' + (clamped % 10u));
    return out;
}

// Pure uptime formatter: "HH:MM:SS" (full) or "MM:SS" / "HHhMM" (compact 160x80).
inline std::string formatUptime(uint32_t uptimeMs, bool compact = false) {
    const uint32_t totalSec = uptimeMs / 1000u;
    const uint32_t secs = totalSec % 60u;
    const uint32_t mins = (totalSec / 60u) % 60u;
    const uint32_t hours = (totalSec / 3600u) % 100u;

    if (compact) {
        if (hours > 0u) {
            return twoDigit(hours) + "h" + twoDigit(mins);
        }
        return twoDigit(mins) + ":" + twoDigit(secs);
    }
    return twoDigit(hours) + ":" + twoDigit(mins) + ":" + twoDigit(secs);
}

// Pure text truncator that fits `maxChars` columns cleanly.
inline std::string truncateText(const std::string& text, std::size_t maxChars) {
    if (maxChars == 0u) {
        return {};
    }
    if (text.size() <= maxChars) {
        return text;
    }
    if (maxChars <= 2u) {
        return text.substr(0u, maxChars);
    }
    return text.substr(0u, maxChars - 2u) + "..";
}

// Pure word-wrapper splitting `text` into at most `maxLines` lines of <= `maxCols` chars.
inline std::vector<std::string> wrapWords(const std::string& text,
                                          std::size_t maxCols,
                                          std::size_t maxLines) {
    if (maxCols == 0u || maxLines == 0u || text.empty()) {
        return {};
    }

    // Tokenize words purely
    std::vector<std::string> words;
    std::string currentWord;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n') {
            if (!currentWord.empty()) {
                words.push_back(currentWord);
                currentWord.clear();
            }
        } else {
            currentWord.push_back(c);
        }
    }
    if (!currentWord.empty()) {
        words.push_back(currentWord);
    }

    std::vector<std::string> lines;
    std::string currentLine;

    for (const std::string& word : words) {
        const std::string clippedWord =
            (word.size() > maxCols) ? truncateText(word, maxCols) : word;

        if (currentLine.empty()) {
            currentLine = clippedWord;
        } else if (currentLine.size() + 1u + clippedWord.size() <= maxCols) {
            currentLine += " " + clippedWord;
        } else {
            lines.push_back(currentLine);
            if (lines.size() >= maxLines) {
                currentLine.clear();
                break;
            }
            currentLine = clippedWord;
        }
    }

    if (!currentLine.empty() && lines.size() < maxLines) {
        lines.push_back(currentLine);
    }

    return lines;
}

struct FaceGeometry {
    int16_t centerX;
    int16_t centerY;
    int16_t eyeOffsetX;
    int16_t eyeY;
    int16_t eyeRadius;
    int16_t mouthY;
    int16_t mouthRadius;
    int16_t bracketOffsetX;
    int16_t bracketHalfHeight;
    int16_t moodBadgeY;
};

struct RadarRowView {
    std::string ssid;
    std::string details;
    int bars;
    bool secure;
};

struct ChannelBarView {
    int32_t channel;
    uint16_t count;
    int16_t heightPx;
};

// Complete immutable UI View-Model ready for a dumb graphics driver to paint.
struct UiFrame {
    ScreenDimensions dims;
    bool compact;
    ColorPalette palette;
    ScreenPage page;

    int16_t headerDividerY;
    int16_t footerDividerY;
    int16_t headerTextY;
    int16_t footerTextY;

    std::string headerLine;
    std::string footerLine;

    // PetFace page fields
    FaceDescriptor face;
    FaceGeometry faceGeom;
    int16_t speechX;
    int16_t speechTopY;
    int16_t lineStepY;
    std::string promptLabel;
    std::vector<std::string> phraseLines;
    std::string spotlightLabel;

    // NearbyRadar page fields
    std::vector<RadarRowView> radarRows;
    std::string radarEmptyHint;

    // ChannelSpectrum page fields
    std::array<ChannelBarView, 13> channelBars;
    int16_t chartBaseY;
    int16_t chartMaxBarH;
    std::string spectrumSummary;
};

// Pure responsive layout & view-model projection: (AppState, ScreenDimensions) -> UiFrame
inline UiFrame buildUiFrame(const AppState& state, ScreenDimensions dims) {
    const bool compact = (dims.width < 200 || dims.height < 100);
    const ColorPalette palette = resolvePalette(state.theme);
    const FaceDescriptor face = faceForMood(state.mood);

    const int16_t headerH = compact ? 12 : 17;
    const int16_t footerH = compact ? 12 : 17;
    const int16_t headerDivY = headerH;
    const int16_t footerDivY = static_cast<int16_t>(dims.height - footerH - 1);

    // Header text (fits exact 6px monospace character budget)
    const std::string chText =
        state.isScanning
            ? "CH:*"
            : (state.latestScan.busiestChannel > 0
                   ? ("CH:" + twoDigit(static_cast<uint32_t>(state.latestScan.busiestChannel)))
                   : "CH:--");

    const std::string apText = "AP:" + std::to_string(state.latestScan.totalAps);
    const std::string upText = "UP:" + formatUptime(state.uptimeMs, compact);
    const std::string batPrefix = state.isCharging ? "+" : "";
    const std::string batText = batPrefix + std::to_string(state.batteryPercent) + "%";

    std::string headerLine;
    if (compact) {
        // <= 26 chars on 160px width
        headerLine = chText + " " + apText + " " + formatUptime(state.uptimeMs, true) + " " + batText;
        headerLine = truncateText(headerLine, 26u);
    } else {
        // <= 39 chars on 240px width
        const std::string peakPart =
            "(" + std::to_string(state.peakApCount) + ")";
        headerLine = chText + "  " + apText + peakPart + "  " + upText + "  " + batText;
        headerLine = truncateText(headerLine, 39u);
    }

    // Footer text
    std::string footerLine;
    if (compact) {
        const std::string statusPart =
            state.isScanning ? "SCAN.." : ("#" + twoDigit(state.completedScans));
        const std::string rssiPart =
            (state.latestScan.totalAps > 0u)
                ? (std::to_string(state.latestScan.strongestRssi) + "dB")
                : "--dB";
        footerLine = statusPart + " " + rssiPart + " [A]VW [B]SCN";
        footerLine = truncateText(footerLine, 26u);
    } else {
        const std::string statusPart =
            state.isScanning ? "SCANNING.." : ("SCAN #" + twoDigit(state.completedScans));
        const std::string rssiPart =
            (state.latestScan.totalAps > 0u)
                ? ("BEST " + std::to_string(state.latestScan.strongestRssi) + "dBm")
                : "BEST --dBm";
        footerLine = statusPart + "  " + rssiPart + "  [A]PG [B]SCN";
        footerLine = truncateText(footerLine, 39u);
    }

    // PetFace geometry
    const int16_t bodyTop = static_cast<int16_t>(headerDivY + 1);
    const int16_t bodyBottom = static_cast<int16_t>(footerDivY - 1);
    const int16_t bodyH = static_cast<int16_t>(bodyBottom - bodyTop);

    FaceGeometry geom{};
    int16_t speechX = 0;
    int16_t speechTopY = 0;
    int16_t lineStepY = 0;
    std::size_t speechCols = 0;
    std::size_t speechMaxLines = 0;

    if (compact) {
        // 160x80 layout
        geom.centerX = 35;
        geom.centerY = static_cast<int16_t>(bodyTop + (bodyH / 2) - 4);
        geom.eyeOffsetX = 11;
        geom.eyeY = static_cast<int16_t>(geom.centerY - 3);
        geom.eyeRadius = 5;
        geom.mouthY = static_cast<int16_t>(geom.centerY + 6);
        geom.mouthRadius = 4;
        geom.bracketOffsetX = 26;
        geom.bracketHalfHeight = 13;
        geom.moodBadgeY = static_cast<int16_t>(bodyBottom - 9);

        speechX = 70;
        speechTopY = static_cast<int16_t>(bodyTop + 3);
        lineStepY = 10;
        speechCols = static_cast<std::size_t>((dims.width - speechX - 3) / 6);
        speechMaxLines = 4u;
    } else {
        // 240x135 layout
        geom.centerX = 55;
        geom.centerY = static_cast<int16_t>(bodyTop + (bodyH / 2) - 7);
        geom.eyeOffsetX = 18;
        geom.eyeY = static_cast<int16_t>(geom.centerY - 4);
        geom.eyeRadius = 8;
        geom.mouthY = static_cast<int16_t>(geom.centerY + 10);
        geom.mouthRadius = 6;
        geom.bracketOffsetX = 42;
        geom.bracketHalfHeight = 20;
        geom.moodBadgeY = static_cast<int16_t>(bodyBottom - 12);

        speechX = 112;
        speechTopY = static_cast<int16_t>(bodyTop + 6);
        lineStepY = 12;
        speechCols = static_cast<std::size_t>((dims.width - speechX - 4) / 6);
        speechMaxLines = 5u;
    }

    const std::string promptLabel = state.petName + ">";
    const std::vector<std::string> wrappedPhrase =
        wrapWords(state.phrase, speechCols, speechMaxLines);

    std::string spotlightLabel;
    if (!compact && state.latestScan.totalAps > 0u && !state.latestScan.strongestSsid.empty()) {
        const std::string rawSpot =
            "> " + state.latestScan.strongestSsid + " (" +
            std::to_string(state.latestScan.strongestRssi) + ")";
        spotlightLabel = truncateText(rawSpot, speechCols);
    }

    // NearbyRadar rows (purely mapped from topNetworks)
    const std::size_t maxRadarRows = compact ? 4u : 6u;
    const std::size_t maxSsidChars = compact ? 12u : 19u;
    const std::vector<AccessPoint> visibleAps = fp::take(state.latestScan.topNetworks, maxRadarRows);
    const std::vector<RadarRowView> radarRows =
        fp::map(visibleAps, [&](const AccessPoint& ap) -> RadarRowView {
            const std::string chStr =
                (ap.channel > 0) ? ("c" + twoDigit(static_cast<uint32_t>(ap.channel))) : "c--";
            const std::string detail = chStr + " " + std::to_string(ap.rssi);
            return RadarRowView{
                truncateText(ap.ssid, maxSsidChars),
                detail,
                rssiToBars(ap.rssi),
                ap.secure
            };
        });

    const std::string radarEmptyHint =
        state.isScanning
            ? "Scanning nearby WiFi..."
            : (state.hasScannedOnce ? "No WiFi APs in range." : "Waiting for first scan...");

    // ChannelSpectrum bars (channels 1..13)
    const int16_t chartBaseY = static_cast<int16_t>(bodyBottom - (compact ? 11 : 14));
    const int16_t chartMaxBarH = static_cast<int16_t>(compact ? 26 : 56);

    uint16_t maxChCount = 1u;
    for (std::size_t ch = 1u; ch <= 13u; ++ch) {
        if (state.latestScan.histogram.counts[ch] > maxChCount) {
            maxChCount = state.latestScan.histogram.counts[ch];
        }
    }

    std::array<ChannelBarView, 13> channelBars{};
    for (std::size_t idx = 0u; idx < 13u; ++idx) {
        const uint16_t c = state.latestScan.histogram.counts[idx + 1u];
        const int16_t barH = (c == 0u)
                                 ? static_cast<int16_t>(0)
                                 : static_cast<int16_t>((std::max)(
                                       2, static_cast<int>((static_cast<int32_t>(c) * chartMaxBarH) /
                                                           static_cast<int32_t>(maxChCount))));
        channelBars[idx] = ChannelBarView{
            static_cast<int32_t>(idx + 1u),
            c,
            barH
        };
    }

    const std::string spectrumSummary =
        compact
            ? ("SEC:" + std::to_string(state.latestScan.secureAps) +
               " OPN:" + std::to_string(state.latestScan.openAps))
            : ("2.4GHz CH 1-13 | SEC:" + std::to_string(state.latestScan.secureAps) +
               " OPEN:" + std::to_string(state.latestScan.openAps));

    return UiFrame{
        dims,
        compact,
        palette,
        state.page,
        headerDivY,
        footerDivY,
        static_cast<int16_t>(compact ? 2 : 5),
        static_cast<int16_t>(footerDivY + (compact ? 3 : 5)),
        headerLine,
        footerLine,
        face,
        geom,
        speechX,
        speechTopY,
        lineStepY,
        promptLabel,
        wrappedPhrase,
        spotlightLabel,
        radarRows,
        radarEmptyHint,
        channelBars,
        chartBaseY,
        chartMaxBarH,
        spectrumSummary
    };
}

// Pure ASCII representation of a UiFrame for Serial Monitor diagnostics and unit tests.
inline std::string renderAsciiPreview(const UiFrame& frame) {
    const std::size_t innerW = frame.compact ? 28u : 40u;
    const auto padLine = [&](const std::string& content) {
        const std::string clipped = truncateText(content, innerW);
        return "| " + clipped + std::string(innerW - clipped.size(), ' ') + " |\n";
    };
    const std::string border = "+-" + std::string(innerW, '-') + "-+\n";

    std::string out = border;
    out += padLine(frame.headerLine);
    out += border;

    if (frame.page == ScreenPage::PetFace) {
        out += padLine(std::string(frame.face.asciiFace) + "  " + frame.promptLabel);
        for (const auto& line : frame.phraseLines) {
            out += padLine("       " + line);
        }
        out += padLine("[" + std::string(frame.face.moodLabel) + "] " + frame.spotlightLabel);
    } else if (frame.page == ScreenPage::NearbyRadar) {
        out += padLine("NEARBY WIFI RADAR");
        if (frame.radarRows.empty()) {
            out += padLine(frame.radarEmptyHint);
        } else {
            for (const auto& row : frame.radarRows) {
                const std::string lockChar = row.secure ? "*" : " ";
                out += padLine(lockChar + row.ssid + " " + row.details);
            }
        }
    } else {
        out += padLine(frame.spectrumSummary);
        std::string countsRow = "CH:";
        for (std::size_t i = 0; i < 13u; ++i) {
            countsRow += std::to_string(frame.channelBars[i].count);
            if (i + 1u < 13u) countsRow += " ";
        }
        out += padLine(countsRow);
    }

    out += border;
    out += padLine(frame.footerLine);
    out += border;
    return out;
}

}  // namespace zd
