#include <cassert>
#include <cctype>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "zd/domain.hpp"
#include "zd/fp.hpp"
#include "zd/pet_logic.hpp"
#include "zd/theme.hpp"
#include "zd/ui_model.hpp"
#include "zd/wifi_analysis.hpp"

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

// WCAG relative luminance / contrast ratio, used to keep every theme readable.
double relativeLuminance(zd::Rgb888 c) {
    const auto channel = [](uint8_t v) {
        const double s = v / 255.0;
        return (s <= 0.03928) ? (s / 12.92) : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}

double contrastRatio(zd::Rgb888 a, zd::Rgb888 b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return ((la > lb ? la : lb) + 0.05) / ((la > lb ? lb : la) + 0.05);
}

void testThemeCatalog() {
    // Catalog shape: one row per ThemeId, in order, unique keys
    static_assert(zd::kThemeCatalog.size() == zd::kThemeCount, "catalog size");
    assert(zd::kThemeCount >= 9u);
    std::set<std::string> keys;
    for (std::size_t i = 0; i < zd::kThemeCount; ++i) {
        const zd::ThemeSpec& t = zd::kThemeCatalog[i];
        assert(zd::themeIndex(t.id) == i);
        assert(keys.insert(t.key).second);

        // Readability: body text >= WCAG AA (4.5:1), accent text >= 3:1
        assert(contrastRatio(t.palette.fg, t.palette.bg) >= 4.5);
        assert(contrastRatio(t.palette.accent, t.palette.bg) >= 3.0);
        // Dividers/hints must still be distinguishable from the background
        assert(contrastRatio(t.palette.muted, t.palette.bg) >= 1.6);
        assert(t.palette.bg != t.palette.fg);

        // Key round-trip (case-insensitive) used by persistence & CLIs
        std::string upper(t.key);
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        assert(zd::themeFromKey(t.key) == t.id);
        assert(zd::themeFromKey(upper) == t.id);

        // RGB565 palette is derived from the same catalog row
        const zd::ColorPalette pal = zd::resolvePalette(t.id);
        assert(pal.bg == zd::toRgb565(t.palette.bg));
        assert(pal.fg == zd::toRgb565(t.palette.fg));
    }
    assert(!zd::themeFromKey("not-a-theme").has_value());
    assert(!zd::themeFromKey("").has_value());
    assert(zd::themeFromKey("neon") == zd::ThemeId::NeonNoir);
    assert(zd::themeFromKey("volt") == zd::ThemeId::NeonVolt);
    assert(zd::themeFromKey("operator") == zd::ThemeId::OperatorBlue);
    assert(zd::themeLabel(zd::ThemeId::NeonNoir) == "NEON NOIR");
    assert(zd::toRgb888Packed({0x12, 0x34, 0x56}) == 0x123456u);

    // Cycling wraps both directions and visits every theme exactly once
    static_assert(zd::nextTheme(zd::ThemeId::PaperInk) == zd::ThemeId::DarkInk, "next");
    static_assert(zd::prevTheme(zd::ThemeId::PaperInk) == zd::ThemeId::SunsetGrid, "prev wrap");
    static_assert(zd::nextTheme(zd::ThemeId::SunsetGrid) == zd::ThemeId::PaperInk, "next wrap");
    static_assert(zd::cycleTheme(zd::ThemeId::PaperInk,
                                 -(2 * static_cast<int>(zd::kThemeCount) + 1)) ==
                      zd::prevTheme(zd::ThemeId::PaperInk),
                  "large negative steps wrap");
    std::set<int> visited;
    zd::ThemeId cur = zd::ThemeId::PaperInk;
    for (std::size_t i = 0; i < zd::kThemeCount; ++i) {
        visited.insert(static_cast<int>(cur));
        cur = zd::nextTheme(cur);
    }
    assert(cur == zd::ThemeId::PaperInk);
    assert(visited.size() == zd::kThemeCount);

    // Invalid ids degrade gracefully (e.g. corrupted persisted byte)
    const auto bogus = static_cast<zd::ThemeId>(200);
    assert(!zd::isValidTheme(bogus));
    assert(zd::themeSpec(bogus).id == zd::ThemeId::PaperInk);

    // Toast visibility is wrap-safe
    assert(!zd::isThemeToastVisible(0u, 5u));
    assert(zd::isThemeToastVisible(2000u, 1999u));
    assert(!zd::isThemeToastVisible(2000u, 2000u));
    assert(zd::isThemeToastVisible(100u, 0xFFFFFF00u));  // millis() rollover
}

void testThemeEventsAndToast() {
    const zd::AppState s0 = zd::initialState(7u);

    // Cycle forward / backward via semantic events
    const zd::AppState sNext = zd::reduce(s0, zd::CycleThemeEvent{5000u, +1});
    assert(sNext.theme == zd::ThemeId::DarkInk);
    assert(sNext.themeToastUntilMs == 5000u + zd::kThemeToastMs);
    assert(sNext.phrase.find("Dark Ink") != std::string::npos);  // pet comments on new look
    assert(s0.theme == zd::ThemeId::PaperInk);                   // s0 untouched

    const zd::AppState sPrev = zd::reduce(s0, zd::CycleThemeEvent{5000u, -1});
    assert(sPrev.theme == zd::ThemeId::SunsetGrid);

    // Direct set (e.g. restored from flash) can be silent
    const zd::AppState sSilent =
        zd::reduce(s0, zd::SetThemeEvent{10u, zd::ThemeId::NeonNoir, false});
    assert(sSilent.theme == zd::ThemeId::NeonNoir);
    assert(sSilent.themeToastUntilMs == 0u);
    assert(sSilent.phrase == s0.phrase);

    // Invalid set is ignored
    const zd::AppState sBad = zd::reduce(sSilent, zd::SetThemeEvent{20u, static_cast<zd::ThemeId>(99)});
    assert(sBad.theme == zd::ThemeId::NeonNoir);

    // Toast appears in the UI frame, centered and on-screen, then expires
    const zd::AppState sToast = zd::reduce(s0, zd::SetThemeEvent{8000u, zd::ThemeId::OperatorBlue});
    for (const zd::ScreenDimensions dims : {zd::ScreenDimensions{240, 135}, zd::ScreenDimensions{160, 80}}) {
        const zd::UiFrame f = zd::buildUiFrame(sToast, dims);
        assert(f.toast.visible);
        assert(f.toast.text.find("OPERATOR") != std::string::npos);
        assert(f.toast.x >= 0 && f.toast.y > f.headerDividerY);
        assert(f.toast.x + f.toast.w <= dims.width);
        assert(f.toast.y + f.toast.h < f.footerDividerY);
        assert(zd::renderAsciiPreview(f).find("OPERATOR") != std::string::npos);
    }
    const zd::AppState sLater = zd::reduce(sToast, zd::TickEvent{8000u + zd::kThemeToastMs, 90, false});
    assert(!zd::buildUiFrame(sLater, {240, 135}).toast.visible);

    // Long-names fit every hardware screen profile (including 128x64 OLED)
    for (const zd::ScreenProfile& prof : zd::kScreenProfiles) {
        assert(zd::screenProfileFromKey(prof.key).has_value());
        for (const zd::ThemeSpec& t : zd::kThemeCatalog) {
            const zd::AppState s = zd::reduce(s0, zd::SetThemeEvent{100u, t.id});
            const zd::UiFrame f = zd::buildUiFrame(s, prof.dims);
            assert(f.toast.visible);
            assert(f.toast.x >= 0 && f.toast.x + f.toast.w <= prof.dims.width);
            assert(f.toast.y > f.headerDividerY && f.toast.y + f.toast.h < f.footerDividerY);
        }
    }

    // Semantic page cycling (forward & backward)
    const zd::AppState sPrevPage = zd::reduce(s0, zd::CyclePageEvent{200u, -1});
    assert(sPrevPage.page == zd::ScreenPage::ChannelSpectrum);
    const zd::AppState sDirectPage =
        zd::reduce(sPrevPage, zd::SetPageEvent{250u, zd::ScreenPage::NearbyRadar});
    assert(sDirectPage.page == zd::ScreenPage::NearbyRadar);

    // Theme change while scanning keeps scan commentary
    const zd::AppState scanning = zd::reduce(s0, zd::ScanStartedEvent{1600u});
    const zd::AppState themedWhileScanning = zd::reduce(scanning, zd::ButtonBLongPressedEvent{1700u});
    assert(themedWhileScanning.phrase == scanning.phrase);
    assert(themedWhileScanning.theme == zd::ThemeId::DarkInk);
}

}  // namespace

int main() {
    testFunctionalCombinators();
    testWifiAnalysisPipeline();
    testStateReducerAndMoods();
    testResponsiveUiModels();
    testThemeCatalog();
    testThemeEventsAndToast();
    std::cout << "All pure functional core & UI layout tests PASSED!\n";
    return 0;
}
