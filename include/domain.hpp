#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace zd {

// Immutable snapshot of a single WiFi access point discovered during a scan.
struct AccessPoint {
    std::string ssid;
    int32_t rssi;
    int32_t channel;
    bool secure;
};

// 2.4 GHz channel counts (index 0 = other/5GHz, indices 1..13 = channels 1..13).
struct ChannelHistogram {
    std::array<uint16_t, 14> counts{};
};

// Immutable aggregated summary computed purely from raw scan results.
struct ScanSummary {
    std::size_t totalAps = 0;
    std::size_t secureAps = 0;
    std::size_t openAps = 0;
    int32_t strongestRssi = -100;
    int32_t busiestChannel = 0;
    std::string strongestSsid{};
    std::vector<AccessPoint> topNetworks{};
    ChannelHistogram histogram{};
};

// Pet emotional states driving faces and status phrases.
enum class Mood : uint8_t {
    Awake = 0,
    LookingLeft,
    LookingRight,
    Happy,
    Excited,
    Cool,
    Lonely,
    Bored,
    Sleepy
};

// Active display screen cycled via Button A.
enum class ScreenPage : uint8_t {
    PetFace = 0,
    NearbyRadar,
    ChannelSpectrum
};

// Color palette themes cycled via Button B long-press.
enum class ThemeId : uint8_t {
    PaperInk = 0,     // High-contrast e-paper look (dark ink on warm light canvas)
    DarkInk,          // Crisp white ink on deep black
    ZeroDayCyan,      // Cyber cyan on midnight background
    PhosphorGreen     // Retro CRT green phosphor on black
};

// Complete immutable application state value.
struct AppState {
    uint32_t uptimeMs = 0;
    uint32_t lastScanStartMs = 0;
    uint32_t lastScanCompleteMs = 0;
    uint32_t lastInteractionMs = 0;
    uint32_t lastPhraseChangeMs = 0;
    uint32_t rngSeed = 0x5EED2026u;
    uint32_t completedScans = 0;
    uint32_t peakApCount = 0;
    bool hasScannedOnce = false;
    bool isScanning = false;
    bool manualScanRequested = false;
    uint8_t scanAnimTick = 0;
    int32_t batteryPercent = 100;
    bool isCharging = false;
    Mood mood = Mood::Awake;
    ScreenPage page = ScreenPage::PetFace;
    ThemeId theme = ThemeId::PaperInk;
    std::string petName = "zeroday";
    std::string phrase = "Hello world! Ready to scan.";
    ScanSummary latestScan{};
};

// --- Algebraic Sum Type of Events fed from the Imperative Shell into the Pure Reducer ---

struct TickEvent {
    uint32_t nowMs;
    int32_t batteryPercent;
    bool isCharging;
};

struct ButtonAPressedEvent {
    uint32_t nowMs;
};

struct ButtonBPressedEvent {
    uint32_t nowMs;
};

struct ButtonBLongPressedEvent {
    uint32_t nowMs;
};

struct ScanStartedEvent {
    uint32_t nowMs;
};

struct ScanCompletedEvent {
    uint32_t nowMs;
    std::vector<AccessPoint> rawNetworks;
};

using AppEvent = std::variant<
    TickEvent,
    ButtonAPressedEvent,
    ButtonBPressedEvent,
    ButtonBLongPressedEvent,
    ScanStartedEvent,
    ScanCompletedEvent>;

}  // namespace zd
