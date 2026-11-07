#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <variant>

#include "domain.hpp"
#include "fp.hpp"
#include "theme.hpp"
#include "wifi_analysis.hpp"

namespace zd {

// Visual building blocks for the Pwnagotchi face renderer (pure data description).
enum class EyeGlyph : uint8_t {
    RoundCenter = 0,
    RoundLeft,
    RoundRight,
    HappyArc,
    WideSparkle,
    Sunglasses,
    SadDroop,
    HalfLid,
    SleepingLine
};

enum class MouthGlyph : uint8_t {
    DoubleSmile = 0,  // Classic Pwnagotchi ‿‿
    BigGrin,          // Open happy D / o mouth
    Smirk,            // Cool subtle half-smile
    FlatLine,         // Neutral/sleepy __
    SadWave           // Lonely frown
};

struct FaceDescriptor {
    EyeGlyph eyes;
    MouthGlyph mouth;
    bool showBlush;
    bool showTear;
    bool showZzz;
    const char* asciiFace;
    const char* moodLabel;
};

// Pure total mapping from Mood -> FaceDescriptor.
constexpr FaceDescriptor faceForMood(Mood mood) {
    switch (mood) {
        case Mood::Awake:
            return {EyeGlyph::RoundCenter, MouthGlyph::DoubleSmile, false, false, false,
                    "(o_o)", "AWAKE"};
        case Mood::LookingLeft:
            return {EyeGlyph::RoundLeft, MouthGlyph::DoubleSmile, false, false, false,
                    "( o_o)", "SCAN"};
        case Mood::LookingRight:
            return {EyeGlyph::RoundRight, MouthGlyph::DoubleSmile, false, false, false,
                    "(o_o )", "SCAN"};
        case Mood::Happy:
            return {EyeGlyph::HappyArc, MouthGlyph::DoubleSmile, true, false, false,
                    "(^_^)", "HAPPY"};
        case Mood::Excited:
            return {EyeGlyph::WideSparkle, MouthGlyph::BigGrin, true, false, false,
                    "(*O*)", "HYPED"};
        case Mood::Cool:
            return {EyeGlyph::Sunglasses, MouthGlyph::Smirk, false, false, false,
                    "[B_B]", "COOL"};
        case Mood::Lonely:
            return {EyeGlyph::SadDroop, MouthGlyph::SadWave, false, true, false,
                    "(;_;)", "LONELY"};
        case Mood::Bored:
            return {EyeGlyph::HalfLid, MouthGlyph::FlatLine, false, false, false,
                    "(._.)", "BORED"};
        case Mood::Sleepy:
            return {EyeGlyph::SleepingLine, MouthGlyph::FlatLine, false, false, true,
                    "(-_-)", "SLEEPY"};
    }
    return {EyeGlyph::RoundCenter, MouthGlyph::DoubleSmile, false, false, false, "(o_o)", "AWAKE"};
}

// Pure cycle helpers for screen pages. (Theme cycling lives in theme.hpp: nextTheme/prevTheme.)
constexpr bool isValidPage(ScreenPage p) {
    return static_cast<uint8_t>(p) <= static_cast<uint8_t>(ScreenPage::ChannelSpectrum);
}

constexpr ScreenPage cyclePage(ScreenPage current, int step) {
    constexpr int kPageCount = 3;
    const int base = isValidPage(current) ? static_cast<int>(current) : 0;
    const int idx = ((base + (step % kPageCount)) % kPageCount + kPageCount) % kPageCount;
    return static_cast<ScreenPage>(idx);
}

constexpr ScreenPage nextPage(ScreenPage current) {
    return cyclePage(current, +1);
}

constexpr ScreenPage prevPage(ScreenPage current) {
    return cyclePage(current, -1);
}

// Pure derivation of pet mood from the latest WiFi scan metrics.
constexpr Mood deriveMoodFromScan(const ScanSummary& summary) {
    if (summary.totalAps == 0) {
        return Mood::Lonely;
    }
    if (summary.totalAps >= 10) {
        return Mood::Excited;
    }
    if (summary.strongestRssi >= -50) {
        return Mood::Cool;
    }
    if (summary.totalAps >= 3) {
        return Mood::Happy;
    }
    return Mood::Bored;
}

// Pure phrase generator threading deterministic PRNG state.
inline fp::RngPick<std::string> selectPhrase(Mood mood, const ScanSummary& scan, uint32_t seed) {
    switch (mood) {
        case Mood::Awake: {
            constexpr std::array<const char*, 4> kPhrases = {
                "Antenna warm! Ready to explore.",
                "Listening to 2.4GHz airwaves...",
                "Press [A] for views, [B] to scan!",
                "ZeroDay awake and feeling curious."
            };
            const auto pick = fp::pickFrom(seed, kPhrases);
            return {std::string(pick.value), pick.nextSeed};
        }
        case Mood::LookingLeft:
        case Mood::LookingRight: {
            constexpr std::array<const char*, 4> kPhrases = {
                "Looking around the airwaves...",
                "Sweeping WiFi channels 1 to 13...",
                "Counting nearby WiFi beacons...",
                "Scanning... anyone broadcasting?"
            };
            const auto pick = fp::pickFrom(seed, kPhrases);
            return {std::string(pick.value), pick.nextSeed};
        }
        case Mood::Happy: {
            const uint32_t s1 = fp::nextSeed(seed);
            if ((s1 % 3u) == 0u && scan.busiestChannel > 0) {
                return {
                    "Spotted " + std::to_string(scan.totalAps) + " APs! Ch " +
                        std::to_string(scan.busiestChannel) + " is cozy.",
                    s1
                };
            }
            constexpr std::array<const char*, 3> kPhrases = {
                "Nice neighborhood! Good signals.",
                "Found friendly routers nearby~",
                "Cruising through clean airwaves!"
            };
            const auto pick = fp::pickFrom(s1, kPhrases);
            return {std::string(pick.value), pick.nextSeed};
        }
        case Mood::Excited: {
            const uint32_t s1 = fp::nextSeed(seed);
            if ((s1 % 2u) == 0u) {
                return {
                    "Whoa! " + std::to_string(scan.totalAps) + " WiFi networks at once!!",
                    s1
                };
            }
            constexpr std::array<const char*, 3> kPhrases = {
                "Spectrum party! Beacons everywhere!",
                "So many signals! My antenna tingles!",
                "Crowded airwaves! Look at all of em!"
            };
            const auto pick = fp::pickFrom(s1, kPhrases);
            return {std::string(pick.value), pick.nextSeed};
        }
        case Mood::Cool: {
            const uint32_t s1 = fp::nextSeed(seed);
            if ((s1 % 2u) == 0u) {
                return {
                    "Crisp " + std::to_string(scan.strongestRssi) + "dBm signal right here!",
                    s1
                };
            }
            constexpr std::array<const char*, 3> kPhrases = {
                "Shades on. Full bars in range.",
                "Crystal clear beacon right next door.",
                "Strongest signal in town. Smooth."
            };
            const auto pick = fp::pickFrom(s1, kPhrases);
            return {std::string(pick.value), pick.nextSeed};
        }
        case Mood::Lonely: {
            constexpr std::array<const char*, 3> kPhrases = {
                "Zero networks... is anyone out there?",
                "Radio silence. Are we underground?",
                "Not a single beacon in sight..."
            };
            const auto pick = fp::pickFrom(seed, kPhrases);
            return {std::string(pick.value), pick.nextSeed};
        }
        case Mood::Bored: {
            constexpr std::array<const char*, 3> kPhrases = {
                "Only a couple faint signals here.",
                "Quiet airwaves... let's take a walk!",
                "Same quiet routers on the horizon."
            };
            const auto pick = fp::pickFrom(seed, kPhrases);
            return {std::string(pick.value), pick.nextSeed};
        }
        case Mood::Sleepy: {
            constexpr std::array<const char*, 3> kPhrases = {
                "Zzz... resting my pixels...",
                "Power nap... press M5 to wake me.",
                "Dreaming of 5-bar beacons... zZz"
            };
            const auto pick = fp::pickFrom(seed, kPhrases);
            return {std::string(pick.value), pick.nextSeed};
        }
    }
    return {"Ready.", fp::nextSeed(seed)};
}

// Pure phrase the pet says when it gets a fresh coat of paint.
inline fp::RngPick<std::string> selectThemePhrase(ThemeId theme, uint32_t seed) {
    const std::string name = themeSpec(theme).name;
    const uint32_t s1 = fp::nextSeed(seed);
    switch (s1 % 4u) {
        case 0u:
            return {"New look: " + name + "!", s1};
        case 1u:
            return {"Ooh, " + name + " suits me.", s1};
        case 2u:
            return {"Repainted in " + name + "~", s1};
        default:
            return {"Fresh pixels! " + name + " mode.", s1};
    }
}

// Pure page transition shared by ButtonA and semantic page events.
inline AppState applyPage(const AppState& state, ScreenPage page, uint32_t nowMs) {
    AppState next = state;
    next.uptimeMs = nowMs;
    next.lastInteractionMs = nowMs;
    next.page = isValidPage(page) ? page : state.page;
    if (!next.isScanning && next.mood == Mood::Sleepy) {
        next.mood = next.hasScannedOnce ? deriveMoodFromScan(next.latestScan) : Mood::Awake;
        const auto picked = selectPhrase(next.mood, next.latestScan, next.rngSeed);
        next.phrase = picked.value;
        next.rngSeed = picked.nextSeed;
        next.lastPhraseChangeMs = nowMs;
    }
    return next;
}

// Pure theme transition shared by every theme-changing event.
inline AppState applyTheme(const AppState& state, ThemeId theme, uint32_t nowMs, bool announce) {
    AppState next = state;
    next.uptimeMs = nowMs;
    next.lastInteractionMs = nowMs;
    next.theme = isValidTheme(theme) ? theme : state.theme;
    if (announce) {
        next.themeToastUntilMs = nowMs + kThemeToastMs;
        if (next.themeToastUntilMs == 0u) {
            next.themeToastUntilMs = 1u;  // 0 is reserved for "no toast"
        }
        // Keep scan commentary intact while the radio is busy.
        if (!next.isScanning) {
            const auto picked = selectThemePhrase(next.theme, next.rngSeed);
            next.phrase = picked.value;
            next.rngSeed = picked.nextSeed;
            next.lastPhraseChangeMs = nowMs;
        }
    }
    return next;
}

// Pure factory constructing initial boot state.
inline AppState initialState(uint32_t bootSeed = 0x5EED2026u) {
    AppState s{};
    s.rngSeed = (bootSeed == 0u) ? 0x5EED2026u : bootSeed;
    s.mood = Mood::Awake;
    s.page = ScreenPage::PetFace;
    s.theme = ThemeId::PaperInk;
    s.petName = "zeroday";
    s.phrase = "Hello world! Ready to scan.";
    return s;
}

// Pure predicate determining whether the imperative shell should launch a new WiFi scan.
constexpr bool shouldTriggerScan(const AppState& state,
                                 uint32_t nowMs,
                                 uint32_t autoIntervalMs = 12000u) {
    if (state.isScanning) {
        return false;
    }
    if (state.manualScanRequested) {
        return true;
    }
    // Perform first automatic scan shortly after boot (1.5s) so boot greeting shows briefly.
    if (!state.hasScannedOnce) {
        return nowMs >= 1500u;
    }
    const uint32_t effectiveInterval =
        (state.mood == Mood::Sleepy) ? (autoIntervalMs * 2u) : autoIntervalMs;
    return (nowMs - state.lastScanCompleteMs) >= effectiveInterval;
}

// Pure state reducer: (AppState, AppEvent) -> AppState
// Never mutates input `state`; returns a new AppState value.
inline AppState reduce(const AppState& state, const AppEvent& event) {
    return std::visit(
        fp::Overloaded{
            [&](const TickEvent& ev) -> AppState {
                AppState next = state;
                next.uptimeMs = ev.nowMs;
                next.batteryPercent = fp::clampVal<int32_t>(ev.batteryPercent, 0, 100);
                next.isCharging = ev.isCharging;

                if (next.isScanning) {
                    // Animate eyes left/right every 450ms during active async WiFi scan.
                    const uint8_t phase =
                        static_cast<uint8_t>(((ev.nowMs - next.lastScanStartMs) / 450u) % 2u);
                    next.scanAnimTick = phase;
                    next.mood = (phase == 0u) ? Mood::LookingLeft : Mood::LookingRight;
                    return next;
                }

                // Transition to Sleepy if idle >= 60s in a quiet WiFi area.
                const bool quietArea = next.latestScan.totalAps <= 2u;
                const bool idleLong = (ev.nowMs - next.lastInteractionMs) >= 60000u &&
                                      ev.nowMs >= 60000u;
                if (quietArea && idleLong && next.mood != Mood::Sleepy) {
                    next.mood = Mood::Sleepy;
                    const auto picked = selectPhrase(Mood::Sleepy, next.latestScan, next.rngSeed);
                    next.phrase = picked.value;
                    next.rngSeed = picked.nextSeed;
                    next.lastPhraseChangeMs = ev.nowMs;
                } else if ((ev.nowMs - next.lastPhraseChangeMs) >= 9000u && next.hasScannedOnce) {
                    const auto picked = selectPhrase(next.mood, next.latestScan, next.rngSeed);
                    next.phrase = picked.value;
                    next.rngSeed = picked.nextSeed;
                    next.lastPhraseChangeMs = ev.nowMs;
                }
                return next;
            },

            [&](const ButtonAPressedEvent& ev) -> AppState {
                return applyPage(state, nextPage(state.page), ev.nowMs);
            },

            [&](const CyclePageEvent& ev) -> AppState {
                return applyPage(state, cyclePage(state.page, ev.step), ev.nowMs);
            },

            [&](const SetPageEvent& ev) -> AppState {
                return applyPage(state, ev.page, ev.nowMs);
            },

            [&](const ButtonBPressedEvent& ev) -> AppState {
                AppState next = state;
                next.uptimeMs = ev.nowMs;
                next.lastInteractionMs = ev.nowMs;
                if (!next.isScanning) {
                    next.manualScanRequested = true;
                }
                return next;
            },

            [&](const ButtonBLongPressedEvent& ev) -> AppState {
                return applyTheme(state, nextTheme(state.theme), ev.nowMs, true);
            },

            [&](const CycleThemeEvent& ev) -> AppState {
                return applyTheme(state, cycleTheme(state.theme, ev.step), ev.nowMs, true);
            },

            [&](const SetThemeEvent& ev) -> AppState {
                return applyTheme(state, ev.theme, ev.nowMs, ev.announce);
            },

            [&](const ScanStartedEvent& ev) -> AppState {
                AppState next = state;
                next.uptimeMs = ev.nowMs;
                next.isScanning = true;
                next.manualScanRequested = false;
                next.lastScanStartMs = ev.nowMs;
                next.scanAnimTick = 0;
                next.mood = Mood::LookingLeft;
                const auto picked = selectPhrase(Mood::LookingLeft, next.latestScan, next.rngSeed);
                next.phrase = picked.value;
                next.rngSeed = picked.nextSeed;
                next.lastPhraseChangeMs = ev.nowMs;
                return next;
            },

            [&](const ScanCompletedEvent& ev) -> AppState {
                AppState next = state;
                next.uptimeMs = ev.nowMs;
                next.isScanning = false;
                next.manualScanRequested = false;
                next.hasScannedOnce = true;
                next.lastScanCompleteMs = ev.nowMs;
                next.completedScans = state.completedScans + 1u;
                next.latestScan = summarizeScan(ev.rawNetworks, 12u);
                if (next.latestScan.totalAps > next.peakApCount) {
                    next.peakApCount = static_cast<uint32_t>(next.latestScan.totalAps);
                }
                next.mood = deriveMoodFromScan(next.latestScan);
                const auto picked = selectPhrase(next.mood, next.latestScan, next.rngSeed);
                next.phrase = picked.value;
                next.rngSeed = picked.nextSeed;
                next.lastPhraseChangeMs = ev.nowMs;
                return next;
            }
        },
        event);
}

}  // namespace zd
