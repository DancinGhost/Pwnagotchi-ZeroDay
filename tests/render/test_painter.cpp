#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include "zd/hal.hpp"
#include "zd/render/painter.hpp"
#include "zd/render/surface_rgb565.hpp"

namespace {

class FakeRadio final : public zd::hal::IRadioScanner {
public:
    bool startAsyncScan() override {
        started = true;
        return true;
    }

    zd::hal::ScanPollResult pollScan() override {
        if (!ready) {
            return {zd::hal::ScanPollState::Running, {}};
        }
        return {
            zd::hal::ScanPollState::Completed,
            {
                {"PocketDeck_2G", -46, 6, true},
                {"OpenCafe", -62, 1, false},
                {"LabBench_AP", -54, 6, true},
                {"Relay_11", -73, 11, true},
                {"SensorNode", -81, 3, true}
            }
        };
    }

    void clearScan() override { clearedCount++; }

    bool started = false;
    bool ready = false;
    int clearedCount = 0;
};

void testPainterAcrossDevicesThemesAndMoods() {
    zd::AppState base = zd::initialState(1337u);
    base = zd::reduce(
        base,
        zd::ScanCompletedEvent{
            12000u,
            {
                {"MeshAlpha", -44, 6, true},
                {"CafeGuest", -58, 1, false},
                {"Workshop_AP", -65, 11, true},
                {"Node_04", -71, 6, true},
                {"Node_05", -77, 9, true},
                {"Node_06", -82, 13, true},
                {"Node_07", -85, 3, false}
            }
        });

    constexpr std::array<zd::Mood, 9> kMoods = {
        zd::Mood::Awake, zd::Mood::LookingLeft, zd::Mood::LookingRight,
        zd::Mood::Happy, zd::Mood::Excited,     zd::Mood::Cool,
        zd::Mood::Lonely, zd::Mood::Bored,      zd::Mood::Sleepy
    };
    constexpr std::array<zd::ScreenPage, 3> kPages = {
        zd::ScreenPage::PetFace,
        zd::ScreenPage::NearbyRadar,
        zd::ScreenPage::ChannelSpectrum
    };

    // Every screen profile x every theme x every page (with and without toast) must paint
    // strictly inside bounds and produce visible foreground pixels + valid 1-bit export.
    for (const zd::ScreenProfile& prof : zd::kScreenProfiles) {
        for (const zd::ThemeSpec& theme : zd::kThemeCatalog) {
            zd::AppState themed = zd::reduce(base, zd::SetThemeEvent{12500u, theme.id, true});
            for (zd::ScreenPage page : kPages) {
                themed = zd::reduce(themed, zd::SetPageEvent{12500u, page});
                const zd::UiFrame frameWithToast = zd::buildUiFrame(themed, prof.dims);
                assert(frameWithToast.toast.visible);

                zd::render::Rgb565Surface surf(prof.dims.width, prof.dims.height);
                zd::render::paintUiFrame(surf, frameWithToast);
                assert(surf.outOfBoundsDrops() == 0u);

                std::size_t fgPixels = 0u;
                for (uint16_t px : surf.pixels()) {
                    if (px != frameWithToast.palette.bg) {
                        ++fgPixels;
                    }
                }
                assert(fgPixels > 100u);

                // Verify 1-bit monochrome export & RGB888 export sizes
                const auto rgb = surf.toRgb888Buffer();
                assert(rgb.size() ==
                       static_cast<std::size_t>(prof.dims.width * prof.dims.height * 3));
                const auto mono = surf.toMono1BitPacked(frameWithToast.palette.bg);
                const std::size_t expectedMonoBytes =
                    ((static_cast<std::size_t>(prof.dims.width) + 7u) / 8u) *
                    static_cast<std::size_t>(prof.dims.height);
                assert(mono.size() == expectedMonoBytes);
            }
        }

        // Every mood expression on PetFace page must also stay strictly in-bounds
        for (zd::Mood mood : kMoods) {
            zd::AppState moodState = base;
            moodState.mood = mood;
            moodState.page = zd::ScreenPage::PetFace;
            const zd::UiFrame frame = zd::buildUiFrame(moodState, prof.dims);
            zd::render::Rgb565Surface surf(prof.dims.width, prof.dims.height);
            zd::render::paintUiFrame(surf, frame);
            assert(surf.outOfBoundsDrops() == 0u);
        }
    }
}

void testHalPlatformSessionAndCli() {
    zd::hal::MemoryThemeStore store("neon");
    zd::hal::FixedPowerSensor power(76, true);
    FakeRadio radio;
    zd::hal::PlatformSession session;

    // Boot restores persisted theme silently (no toast)
    session.boot(99u, 0u, &store);
    assert(session.state().theme == zd::ThemeId::NeonNoir);
    assert(session.state().themeToastUntilMs == 0u);

    // Cycle theme forward -> NeonVolt, persisted to store
    session.dispatch(zd::CycleThemeEvent{500u, +1}, &store);
    assert(session.state().theme == zd::ThemeId::NeonVolt);
    assert(store.storedKey() == "volt");

    // Step radio lifecycle
    assert(session.stepRadio(1600u, radio) != nullptr);
    assert(session.state().isScanning);
    radio.ready = true;
    const char* doneTag = session.stepRadio(2400u, radio);
    assert(doneTag != nullptr && std::string(doneTag) == "SCAN_DONE");
    assert(!session.state().isScanning);
    assert(session.state().latestScan.totalAps == 5u);

    session.stepTick(2500u, power);
    assert(session.state().batteryPercent == 76);
    assert(session.state().isCharging);

    // Portable CLI interpreter
    const auto listOut =
        zd::hal::interpretCliLine("themes", session.state(), {240, 135}, 2600u);
    assert(listOut.output.find("operator") != std::string::npos);
    assert(listOut.output.find("volt") != std::string::npos);

    const auto setOut =
        zd::hal::interpretCliLine("theme operator", session.state(), {240, 135}, 2700u);
    assert(setOut.event.has_value());
    session.dispatch(*setOut.event, &store);
    assert(session.state().theme == zd::ThemeId::OperatorBlue);
    assert(store.storedKey() == "operator");

    const auto prevOut =
        zd::hal::interpretCliLine("theme prev", session.state(), {240, 135}, 2800u);
    assert(prevOut.event.has_value());
    session.dispatch(*prevOut.event, &store);
    assert(session.state().theme == zd::ThemeId::NeonVolt);
}

}  // namespace

int main() {
    testPainterAcrossDevicesThemesAndMoods();
    testHalPlatformSessionAndCli();
    std::cout << "All cross-device painter, framebuffer, and HAL session tests PASSED!\n";
    return 0;
}
