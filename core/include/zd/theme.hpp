#pragma once

// Data-driven, platform-neutral theme catalog.
//
// Every display backend (M5GFX TFTs, the web canvas, desktop terminals, future
// e-paper / SDL / framebuffer targets) reads the *same* table below and converts the
// 24-bit colors into whatever its hardware wants. Adding a theme is two steps:
//   1. Append an id to `ThemeId` in domain.hpp (before `Count_`).
//   2. Append a matching row to `kThemeCatalog` here (and mirror it in web/js/themes.js;
//      the Node test suite fails if the two catalogs drift apart).
//
// Naming policy: theme names describe a *mood or aesthetic* using generic words only.
// Never use product names, distro names, game/film titles, logos, or brand-exact colors
// (see docs/THEMES.md).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "domain.hpp"

namespace zd {

// Platform-neutral 24-bit sRGB color.
struct Rgb888 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

constexpr bool operator==(Rgb888 a, Rgb888 b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}
constexpr bool operator!=(Rgb888 a, Rgb888 b) {
    return !(a == b);
}

// Pure constexpr 24-bit RGB -> 16-bit RGB565 conversion (ST7789 / ILI9341 style TFTs).
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((static_cast<uint16_t>(r & 0xF8u)) << 8) |
                                 ((static_cast<uint16_t>(g & 0xFCu)) << 3) |
                                 (static_cast<uint16_t>(b) >> 3));
}
constexpr uint16_t toRgb565(Rgb888 c) {
    return rgb565(c.r, c.g, c.b);
}

// Packed 0xRRGGBB (handy for LVGL, SDL, CSS hex, ANSI truecolor, etc.).
constexpr uint32_t toRgb888Packed(Rgb888 c) {
    return (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) |
           static_cast<uint32_t>(c.b);
}

// Fast integer perceived luminance (0..255) for monochrome / 1-bit e-paper & OLED drivers.
constexpr uint8_t rgbLuma(Rgb888 c) {
    return static_cast<uint8_t>((77u * static_cast<uint32_t>(c.r) +
                                 150u * static_cast<uint32_t>(c.g) +
                                 29u * static_cast<uint32_t>(c.b)) >>
                                8u);
}

// Unpack 16-bit RGB565 back to 24-bit Rgb888 (for framebuffer export / mono thresholding).
constexpr Rgb888 fromRgb565(uint16_t px) {
    const uint8_t r5 = static_cast<uint8_t>((px >> 11u) & 0x1Fu);
    const uint8_t g6 = static_cast<uint8_t>((px >> 5u) & 0x3Fu);
    const uint8_t b5 = static_cast<uint8_t>(px & 0x1Fu);
    return Rgb888{
        static_cast<uint8_t>((r5 << 3u) | (r5 >> 2u)),
        static_cast<uint8_t>((g6 << 2u) | (g6 >> 4u)),
        static_cast<uint8_t>((b5 << 3u) | (b5 >> 2u))
    };
}

// Semantic color roles consumed by every renderer.
struct ThemePalette {
    Rgb888 bg;         // Screen background
    Rgb888 fg;         // Primary ink: face, body text, signal bars
    Rgb888 accent;     // Prompt label, footer, secondary details
    Rgb888 muted;      // Dividers, hints, inactive meter ticks
    Rgb888 highlight;  // Blush, grin fill, warm "pop" color
};

struct ThemeSpec {
    ThemeId id;
    const char* key;       // Stable lowercase slug for persistence / CLI / URLs. Never rename.
    const char* name;      // Human-readable display name (<= 12 chars, ASCII)
    const char* blurb;     // One-line description for menus and docs
    bool lightBackground;  // Hint for mono / e-paper backends and surrounding UI chrome
    ThemePalette palette;
};

// clang-format off
inline constexpr std::array<ThemeSpec, kThemeCount> kThemeCatalog = {{
    {ThemeId::PaperInk, "paper", "Paper Ink",
     "Classic e-paper: charcoal ink on warm paper", true,
     {{242, 240, 230}, {22, 24, 28}, {48, 58, 92}, {155, 152, 142}, {215, 95, 110}}},

    {ThemeId::DarkInk, "dark", "Dark Ink",
     "High-contrast white ink on deep black", false,
     {{10, 12, 16}, {244, 246, 250}, {165, 195, 255}, {72, 78, 92}, {255, 130, 150}}},

    {ThemeId::ZeroDayCyan, "cyan", "ZeroDay",
     "Signature cyber cyan on midnight blue", false,
     {{6, 12, 22}, {0, 238, 255}, {120, 255, 200}, {30, 68, 96}, {255, 90, 180}}},

    {ThemeId::PhosphorGreen, "green", "Phosphor",
     "Retro green-phosphor CRT terminal", false,
     {{6, 12, 6}, {56, 255, 96}, {175, 255, 175}, {26, 86, 36}, {120, 255, 120}}},

    {ThemeId::NeonNoir, "neon", "Neon Noir",
     "Rain-slick neon city: hot magenta and electric cyan", false,
     {{14, 6, 26}, {255, 72, 196}, {0, 232, 255}, {104, 52, 150}, {255, 196, 56}}},

    {ThemeId::NeonVolt, "volt", "Neon Volt",
     "High-voltage visor: acid yellow and electric cyan", false,
     {{12, 14, 20}, {245, 240, 56}, {40, 240, 232}, {76, 84, 104}, {255, 72, 148}}},

    {ThemeId::OperatorBlue, "operator", "Operator",
     "Pentest workstation: steel text, cobalt blue, red alerts", false,
     {{14, 18, 28}, {216, 226, 240}, {78, 150, 255}, {66, 80, 110}, {240, 76, 92}}},

    {ThemeId::RedTeam, "redteam", "Red Team",
     "Night-ops red that keeps your eyes dark-adapted", false,
     {{16, 4, 6}, {255, 86, 86}, {255, 164, 104}, {124, 40, 44}, {255, 220, 150}}},

    {ThemeId::AmberCrt, "amber", "Amber CRT",
     "Vintage amber monochrome terminal glow", false,
     {{16, 10, 2}, {255, 178, 0}, {255, 216, 128}, {100, 64, 10}, {255, 124, 40}}},

    {ThemeId::SunsetGrid, "sunset", "Sunset Grid",
     "Synthwave sunset: warm orange and pink on deep purple", false,
     {{28, 10, 46}, {255, 156, 96}, {255, 92, 178}, {96, 52, 136}, {90, 220, 255}}},
}};
// clang-format on

// How long the "theme changed" overlay stays on screen after a switch.
inline constexpr uint32_t kThemeToastMs = 1600u;

// --- Compile-time catalog validation -------------------------------------------------

namespace detail {

constexpr std::size_t cstrLen(const char* s) {
    std::size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

constexpr bool cstrEq(const char* a, const char* b) {
    std::size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return false;
        }
        ++i;
    }
    return a[i] == b[i];
}

constexpr bool isLowerSlug(const char* s) {
    if (s[0] == '\0') {
        return false;
    }
    for (std::size_t i = 0; s[i] != '\0'; ++i) {
        const char c = s[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

constexpr bool catalogIsWellFormed() {
    for (std::size_t i = 0; i < kThemeCatalog.size(); ++i) {
        const ThemeSpec& t = kThemeCatalog[i];
        if (static_cast<std::size_t>(t.id) != i) return false;      // rows ordered by id
        if (!isLowerSlug(t.key)) return false;                      // stable slug keys
        if (cstrLen(t.name) == 0 || cstrLen(t.name) > 12) return false;  // fits 160px toast
        for (std::size_t j = i + 1; j < kThemeCatalog.size(); ++j) {
            if (cstrEq(t.key, kThemeCatalog[j].key)) return false;  // unique keys
        }
    }
    return true;
}

}  // namespace detail

static_assert(kThemeCount > 0, "Theme catalog must not be empty");
static_assert(detail::catalogIsWellFormed(),
              "kThemeCatalog rows must match ThemeId order, use unique lowercase keys, "
              "and have names of 1..12 chars");

// --- Pure catalog queries ------------------------------------------------------------

constexpr std::size_t themeIndex(ThemeId id) {
    return static_cast<std::size_t>(id);
}

constexpr bool isValidTheme(ThemeId id) {
    return themeIndex(id) < kThemeCount;
}

// Total lookup: invalid ids fall back to the first catalog entry.
constexpr const ThemeSpec& themeSpec(ThemeId id) {
    return kThemeCatalog[isValidTheme(id) ? themeIndex(id) : 0u];
}

// Wrap-around step through the catalog (step may be negative).
constexpr ThemeId cycleTheme(ThemeId current, int step) {
    const int n = static_cast<int>(kThemeCount);
    const int base = isValidTheme(current) ? static_cast<int>(themeIndex(current)) : 0;
    const int idx = ((base + (step % n)) % n + n) % n;
    return kThemeCatalog[static_cast<std::size_t>(idx)].id;
}

constexpr ThemeId nextTheme(ThemeId current) {
    return cycleTheme(current, +1);
}

constexpr ThemeId prevTheme(ThemeId current) {
    return cycleTheme(current, -1);
}

// Case-insensitive key lookup (persistence, serial CLI, web, config files).
inline std::optional<ThemeId> themeFromKey(std::string_view key) {
    const auto lower = [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    for (const ThemeSpec& t : kThemeCatalog) {
        const std::string_view k(t.key);
        if (k.size() != key.size()) {
            continue;
        }
        bool match = true;
        for (std::size_t i = 0; i < k.size(); ++i) {
            if (lower(key[i]) != k[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            return t.id;
        }
    }
    return std::nullopt;
}

// Uppercase ASCII label for tiny pixel fonts ("Neon Noir" -> "NEON NOIR").
inline std::string themeLabel(ThemeId id) {
    std::string out(themeSpec(id).name);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

// Wrap-safe check: is the theme-change toast still visible at `nowMs`?
constexpr bool isThemeToastVisible(uint32_t toastUntilMs, uint32_t nowMs) {
    return toastUntilMs != 0u && static_cast<int32_t>(toastUntilMs - nowMs) > 0;
}

}  // namespace zd
