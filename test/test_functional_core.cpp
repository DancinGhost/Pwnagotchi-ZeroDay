#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "domain.hpp"
#include "fp.hpp"
#include "pet_logic.hpp"
#include "ui_model.hpp"
#include "wifi_analysis.hpp"

namespace {

void testFunctionalCombinators() {
    const std::vector<int> nums = {5, 1, 4, 2, 3, 6};

    // Original input remains untouched through a pure pipeline
    const std::vector<int> result = fp::pipe(
        nums,
        fp::filtered([](int x) { return (x % 2) == 0; }),
        fp::mapped([](int x) { return x * 10; }),
        fp::sortedBy([](int a, int b) { return a > b; }),
        fp::taken(2));

    assert((nums == std::vector<int>{5, 1, 4, 2, 3, 6}));
    assert((result == std::vector<int>{60, 40}));

    const int sum = fp::foldl(nums, 0, [](int acc, int x) { return acc + x; });
    assert(sum == 21);

    // Deterministic pure PRNG
    constexpr std::array<const char*, 3> kItems = {"alpha", "beta", "gamma"};
    const auto p1 = fp::pickFrom(12345u, kItems);
    const auto p2 = fp::pickFrom(12345u, kItems);
    assert(std::string(p1.value) == std::string(p2.value));
    assert(p1.nextSeed == p2.nextSeed);
}

void testWifiAnalysisPipeline() {
    const std::vector<zd::AccessPoint> raw = {
        {"MeshNet", -72, 6, true},
        {"MeshNet", -49, 6, true},   // Stronger node of same SSID
        {"", -81, 11, true},         // Hidden SSID
        {"CafeFree", -63, 1, false},
        {"LabRouter\n", -55, 6, true}
    };

    const zd::ScanSummary summary = zd::summarizeScan(raw, 6u);
    assert(summary.totalAps == 5u);
    assert(summary.secureAps == 4u);
    assert(summary.openAps == 1u);
    assert(summary.busiestChannel == 6);
    assert(summary.strongestRssi == -49);
    assert(summary.strongestSsid == "MeshNet");

    // Deduplicated top list has 4 unique entries, sorted strongest-first
    assert(summary.topNetworks.size() == 4u);
    assert(summary.topNetworks[0].ssid == "MeshNet");
    assert(summary.topNetworks[0].rssi == -49);
    assert(summary.topNetworks[1].ssid == "LabRouter");
    assert(summary.topNetworks[2].ssid == "CafeFree");
    assert(summary.topNetworks[3].ssid == "<hidden>");

    assert(zd::rssiToBars(-48) == 4);
    assert(zd::rssiToBars(-62) == 3);
    assert(zd::rssiToBars(-75) == 2);
    assert(zd::rssiToBars(-85) == 1);
    assert(zd::rssiToBars(-94) == 0);
}

void testStateReducerAndMoods() {
    const zd::AppState s0 = zd::initialState(42u);
    assert(!zd::shouldTriggerScan(s0, 500u));
    assert(zd::shouldTriggerScan(s0, 1600u));

    // Start scan -> LookingLeft, then Tick +500ms -> LookingRight
    const zd::AppState s1 = zd::reduce(s0, zd::ScanStartedEvent{1600u});
    assert(s1.isScanning);
    assert(s1.mood == zd::Mood::LookingLeft);
    assert(!s0.isScanning);  // Verify s0 was not mutated

    const zd::AppState s2 = zd::reduce(s1, zd::TickEvent{2100u, 88, false});
    assert(s2.mood == zd::Mood::LookingRight);
    assert(s2.batteryPercent == 88);

    // Finish scan with 0 APs -> Lonely
    const zd::AppState sLonely = zd::reduce(s2, zd::ScanCompletedEvent{3200u, {}});
    assert(!sLonely.isScanning);
    assert(sLonely.mood == zd::Mood::Lonely);
    assert(sLonely.completedScans == 1u);

    // Strong nearby AP (-45 dBm) -> Cool
    const zd::AppState sCool = zd::reduce(
        sLonely,
        zd::ScanCompletedEvent{15500u, {{"ZeroDayLab", -45, 6, true}, {"Guest", -75, 1, false}}});
    assert(sCool.mood == zd::Mood::Cool);

    // 11 APs -> Excited
    std::vector<zd::AccessPoint> crowd;
    for (int i = 0; i < 11; ++i) {
        crowd.push_back({"AP_" + std::to_string(i), -65 - i, (i % 11) + 1, true});
    }
    const zd::AppState sExcited = zd::reduce(sCool, zd::ScanCompletedEvent{28000u, crowd});
    assert(sExcited.mood == zd::Mood::Excited);
    assert(sExcited.peakApCount == 11u);

    // Idle > 60s in quiet area -> Sleepy, then wake via Button A
    const zd::AppState sQuiet =
        zd::reduce(sExcited, zd::ScanCompletedEvent{30000u, {{"FaintAP", -84, 11, true}}});
    assert(sQuiet.mood == zd::Mood::Bored);

    const zd::AppState sSleepy = zd::reduce(sQuiet, zd::TickEvent{95000u, 80, true});
    assert(sSleepy.mood == zd::Mood::Sleepy);
    assert(sSleepy.isCharging);

    const zd::AppState sWoken = zd::reduce(sSleepy, zd::ButtonAPressedEvent{96000u});
    assert(sWoken.mood == zd::Mood::Bored);
    assert(sWoken.page == zd::ScreenPage::NearbyRadar);

    // Theme & Manual Scan via Button B
    const zd::AppState sThemed = zd::reduce(sWoken, zd::ButtonBLongPressedEvent{96500u});
    assert(sThemed.theme == zd::ThemeId::DarkInk);

    const zd::AppState sManual = zd::reduce(sThemed, zd::ButtonBPressedEvent{97000u});
    assert(sManual.manualScanRequested);
    assert(zd::shouldTriggerScan(sManual, 97010u));
}

void testResponsiveUiModels() {
    zd::AppState state = zd::initialState(99u);
    state = zd::reduce(
        state,
        zd::ScanCompletedEvent{
            75000u,
            {
                {"CyberDeck_2G", -47, 6, true},
                {"CoffeeShop_Guest", -61, 1, false},
                {"M5_Mesh_Node", -68, 6, true},
                {"Neighbor_WiFi", -79, 11, true}
            }
        });

    // Full M5StickC Plus / Plus2 resolution (240x135)
    const zd::UiFrame plusFrame = zd::buildUiFrame(state, {240, 135});
    assert(!plusFrame.compact);
    assert(plusFrame.headerLine.size() <= 39u);
    assert(plusFrame.footerLine.size() <= 39u);
    for (const auto& line : plusFrame.phraseLines) {
        assert(line.size() <= 20u);
    }

    // Compact M5StickC resolution (160x80)
    const zd::UiFrame compactFrame = zd::buildUiFrame(state, {160, 80});
    assert(compactFrame.compact);
    assert(compactFrame.headerLine.size() <= 26u);
    assert(compactFrame.footerLine.size() <= 26u);
    for (const auto& line : compactFrame.phraseLines) {
        assert(line.size() <= 14u);
    }

    std::cout << "=== M5StickC Plus/Plus2 (240x135) Face View Preview ===\n"
              << zd::renderAsciiPreview(plusFrame) << "\n";

    const zd::AppState radarState = zd::reduce(state, zd::ButtonAPressedEvent{76000u});
    std::cout << "=== M5StickC Plus/Plus2 (240x135) WiFi Radar Preview ===\n"
              << zd::renderAsciiPreview(zd::buildUiFrame(radarState, {240, 135})) << "\n";

    const zd::AppState specState = zd::reduce(radarState, zd::ButtonAPressedEvent{77000u});
    std::cout << "=== M5StickC Plus/Plus2 (240x135) Channel Spectrum Preview ===\n"
              << zd::renderAsciiPreview(zd::buildUiFrame(specState, {240, 135})) << "\n";
}

}  // namespace

int main() {
    testFunctionalCombinators();
    testWifiAnalysisPipeline();
    testStateReducerAndMoods();
    testResponsiveUiModels();
    std::cout << "All pure functional core & UI layout tests PASSED!\n";
    return 0;
}
