#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domain.hpp"
#include "fp.hpp"
#include "pet_logic.hpp"
#include "theme.hpp"
#include "wifi_analysis.hpp"

namespace zd {

struct ScreenDimensions {
    int16_t width = 240;
    int16_t height = 135;
};

struct ScreenProfile {
    const char* key;
    const char* label;
    ScreenDimensions dims;
    bool monochrome;
};

// Common hardware display profiles across target form factors.
// `buildUiFrame` works with *any* arbitrary ScreenDimensions, not just these presets.
inline constexpr std::array<ScreenProfile, 5> kScreenProfiles = {{
    {"micro",   "128x64 OLED (SSD1306 / SH1106)",            {128, 64},  true},
    {"compact", "160x80 TFT (M5StickC / 0.96\" IPS)",        {160, 80},  false},
    {"plus",    "240x135 TFT (StickC Plus2 / Cardputer)",    {240, 135}, false},
    {"epaper",  "250x122 e-Paper (2.13\" HAT)",              {250, 122}, true},
    {"wide",    "320x240 QVGA (Core2 / CYD / T-Deck)",       {320, 240}, false},
}};

inline std::optional<ScreenProfile> screenProfileFromKey(std::string_view key) {
    for (const ScreenProfile& p : kScreenProfiles) {
        if (key == p.key) {
            return p;
        }
    }
    return std::nullopt;
}

// Device-ready RGB565 palette (native format of the ST7789/ILI9341 TFTs used by
// M5Stack/M5Stick and most ESP32 boards). Backends needing other color formats can
// read the platform-neutral `ThemePalette` via `themeSpec(frame.theme).palette`.
struct ColorPalette {
    uint16_t bg;
    uint16_t fg;
    uint16_t accent;
    uint16_t muted;
    uint16_t highlight;
};

constexpr ColorPalette toColorPalette(const ThemePalette& p) {
    return ColorPalette{
        toRgb565(p.bg),
        toRgb565(p.fg),
        toRgb565(p.accent),
        toRgb565(p.muted),
        toRgb565(p.highlight)
    };
}

// Pure theme -> RGB565 palette mapping (driven by the data catalog in theme.hpp).
constexpr ColorPalette resolvePalette(ThemeId theme) {
    return toColorPalette(themeSpec(theme).palette);
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

// Transient overlay announcing the active theme after a switch.
struct ToastView {
    bool visible = false;
    std::string text{};
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;
    int16_t textX = 0;
    int16_t textY = 0;
};

// Pure toast layout: centered in the body region, sized for the 6x8 px Font0 glyphs.
inline ToastView buildThemeToast(const AppState& state,
                                 ScreenDimensions dims,
                                 bool compact,
                                 int16_t bodyTop,
                                 int16_t bodyBottom) {
    ToastView t{};
    if (!isThemeToastVisible(state.themeToastUntilMs, state.uptimeMs)) {
        return t;
    }
    const std::string label = themeLabel(state.theme);
    const std::string position =
        std::to_string(themeIndex(state.theme) + 1u) + "/" + std::to_string(kThemeCount);
    const std::size_t maxChars = static_cast<std::size_t>((dims.width - 16) / 6);
    t.text = truncateText(compact ? ("< " + label + " >")
                                  : ("THEME " + position + ": " + label),
                          maxChars);

    const int16_t padX = compact ? 5 : 8;
    const int16_t padY = compact ? 4 : 6;
    t.w = static_cast<int16_t>(static_cast<int16_t>(t.text.size()) * 6 + padX * 2);
    t.h = static_cast<int16_t>(8 + padY * 2);
    t.x = static_cast<int16_t>((dims.width - t.w) / 2);
    t.y = static_cast<int16_t>(bodyTop + ((bodyBottom - bodyTop) - t.h) / 2);
    t.textX = static_cast<int16_t>(t.x + padX);
    t.textY = static_cast<int16_t>(t.y + padY);
    t.visible = true;
    return t;
}

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

    // Theming
    ThemeId theme;
    ToastView toast;
};

// Pure responsive layout & view-model projection: (AppState, ScreenDimensions) -> UiFrame
// Scales cleanly across micro OLEDs (128x64), compact sticks (160x80), pocket TFTs &
// 2.13" e-paper HATs (240x135 / 250x122), and QVGA cyberdecks (320x240+).
inline UiFrame buildUiFrame(const AppState& state, ScreenDimensions dims) {
    dims.width = (std::max)(static_cast<int16_t>(96), dims.width);
    dims.height = (std::max)(static_cast<int16_t>(56), dims.height);

    const bool micro = (dims.width < 150 || dims.height < 72);
    const bool compact = (dims.width < 200 || dims.height < 100);
    const bool wide = (dims.width >= 300 && dims.height >= 180);
    const ColorPalette palette = resolvePalette(state.theme);
    const FaceDescriptor face = faceForMood(state.mood);

    const int16_t headerH = micro ? 10 : (compact ? 12 : 17);
    const int16_t footerH = micro ? 10 : (compact ? 12 : 17);
    const int16_t headerDivY = headerH;
    const int16_t footerDivY = static_cast<int16_t>(dims.height - footerH - 1);

    // Dynamic 6px monospace character budget (20 on 128px, 26 on 160px, 39 on 240px, 52 on 320px).
    const std::size_t maxFrameCols =
        static_cast<std::size_t>((std::max)(12, (static_cast<int>(dims.width) - 4) / 6));

    // Header text
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
    if (micro) {
        headerLine = chText + " " + apText + " " + batText;
    } else if (compact) {
        headerLine = chText + " " + apText + " " + formatUptime(state.uptimeMs, true) + " " + batText;
    } else {
        const std::string peakPart =
            "(" + std::to_string(state.peakApCount) + ")";
        headerLine = chText + "  " + apText + peakPart + "  " + upText + "  " + batText;
    }
    headerLine = truncateText(headerLine, maxFrameCols);

    // Footer text
    std::string footerLine;
    if (micro) {
        const std::string statusPart =
            state.isScanning ? "SCN.." : ("#" + twoDigit(state.completedScans));
        const std::string rssiPart =
            (state.latestScan.totalAps > 0u)
                ? (std::to_string(state.latestScan.strongestRssi) + "dB")
                : "--dB";
        footerLine = statusPart + " " + rssiPart + " [A]/[B]";
    } else if (compact) {
        const std::string statusPart =
            state.isScanning ? "SCAN.." : ("#" + twoDigit(state.completedScans));
        const std::string rssiPart =
            (state.latestScan.totalAps > 0u)
                ? (std::to_string(state.latestScan.strongestRssi) + "dB")
                : "--dB";
        footerLine = statusPart + " " + rssiPart + " [A]VW [B]SCN";
    } else {
        const std::string statusPart =
            state.isScanning ? "SCANNING.." : ("SCAN #" + twoDigit(state.completedScans));
        const std::string rssiPart =
            (state.latestScan.totalAps > 0u)
                ? ("BEST " + std::to_string(state.latestScan.strongestRssi) + "dBm")
                : "BEST --dBm";
        footerLine = statusPart + "  " + rssiPart + "  [A]PG [B]SCN";
    }
    footerLine = truncateText(footerLine, maxFrameCols);

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

    if (micro) {
        // 128x64 OLED layout
        geom.centerX = 28;
        geom.centerY = static_cast<int16_t>(bodyTop + (bodyH / 2) - 4);
        geom.eyeOffsetX = 9;
        geom.eyeY = static_cast<int16_t>(geom.centerY - 2);
        geom.eyeRadius = 4;
        geom.mouthY = static_cast<int16_t>(geom.centerY + 5);
        geom.mouthRadius = 3;
        geom.bracketOffsetX = 21;
        geom.bracketHalfHeight = 10;
        geom.moodBadgeY = static_cast<int16_t>(bodyBottom - 8);

        speechX = 56;
        speechTopY = static_cast<int16_t>(bodyTop + 2);
        lineStepY = 9;
        speechCols = static_cast<std::size_t>((std::max)(6, (dims.width - speechX - 2) / 6));
        speechMaxLines = static_cast<std::size_t>((std::max)(2, (bodyH - 12) / 9));
    } else if (compact) {
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
    } else if (wide) {
        // 320x240+ QVGA cyberdeck layout
        geom.centerX = 72;
        geom.centerY = static_cast<int16_t>(bodyTop + (bodyH / 2) - 8);
        geom.eyeOffsetX = 22;
        geom.eyeY = static_cast<int16_t>(geom.centerY - 5);
        geom.eyeRadius = 10;
        geom.mouthY = static_cast<int16_t>(geom.centerY + 13);
        geom.mouthRadius = 8;
        geom.bracketOffsetX = 54;
        geom.bracketHalfHeight = 26;
        geom.moodBadgeY = static_cast<int16_t>(bodyBottom - 14);

        speechX = 144;
        speechTopY = static_cast<int16_t>(bodyTop + 8);
        lineStepY = 14;
        speechCols = static_cast<std::size_t>((dims.width - speechX - 6) / 6);
        speechMaxLines = static_cast<std::size_t>((std::max)(5, (bodyH - 38) / 14));
    } else {
        // 240x135 / 250x122 standard pocket & e-paper layout
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
        speechMaxLines = (bodyH >= 92) ? 5u : 4u;
    }

    const std::string promptLabel = truncateText(state.petName + ">", speechCols);
    const std::vector<std::string> wrappedPhrase =
        wrapWords(state.phrase, speechCols, speechMaxLines);

    std::string spotlightLabel;
    if (!compact && state.latestScan.totalAps > 0u && !state.latestScan.strongestSsid.empty()) {
        const std::string rawSpot =
            "> " + state.latestScan.strongestSsid + " (" +
            std::to_string(state.latestScan.strongestRssi) + ")";
        spotlightLabel = truncateText(rawSpot, speechCols);
    }

    // NearbyRadar rows (purely mapped from topNetworks, scaled to screen height & width)
    const int16_t radarRowStep = compact ? 12 : 15;
    const std::size_t maxRadarRows = static_cast<std::size_t>(
        (std::max)(2, (static_cast<int>(bodyH) - (compact ? 4 : 5)) / radarRowStep));
    const std::size_t maxSsidChars = static_cast<std::size_t>(
        (std::max)(6, (static_cast<int>(dims.width) - (micro ? 80 : (compact ? 88 : 126))) / 6));
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
        truncateText(state.isScanning
                         ? "Scanning nearby WiFi..."
                         : (state.hasScannedOnce ? "No WiFi APs in range."
                                                 : "Waiting for first scan..."),
                     maxFrameCols);

    // ChannelSpectrum bars (channels 1..13, scaled to body height)
    const int16_t chartBaseY = static_cast<int16_t>(bodyBottom - (micro ? 9 : (compact ? 11 : 14)));
    const int16_t chartMaxBarH = static_cast<int16_t>(
        (std::max)(14, static_cast<int>(chartBaseY - bodyTop) - (micro ? 12 : (compact ? 16 : 29))));

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
        static_cast<int16_t>(micro ? 1 : (compact ? 2 : 5)),
        static_cast<int16_t>(footerDivY + (micro ? 2 : (compact ? 3 : 5))),
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
        spectrumSummary,
        state.theme,
        buildThemeToast(state, dims, compact, bodyTop, bodyBottom)
    };
}

// Pure ASCII representation of a UiFrame for Serial Monitor diagnostics and unit tests.
inline std::string renderAsciiPreview(const UiFrame& frame) {
    const std::size_t innerW =
        (frame.dims.width == 160)
            ? 28u
            : static_cast<std::size_t>((std::max)(22, static_cast<int>(frame.dims.width) / 6));
    const auto padLine = [&](const std::string& content) {
        const std::string clipped = truncateText(content, innerW);
        return "| " + clipped + std::string(innerW - clipped.size(), ' ') + " |\n";
    };
    const std::string border = "+-" + std::string(innerW, '-') + "-+\n";

    std::string out = border;
    out += padLine(frame.headerLine);
    out += border;

    if (frame.toast.visible) {
        out += padLine("** " + frame.toast.text + " **");
    }

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
