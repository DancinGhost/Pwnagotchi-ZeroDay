// Imperative Hardware Shell for pwnagotchi-zeroday (M5StickC / Plus / Plus2, and other
// M5Unified-supported ESP32 boards such as M5Stack Core / Core2 / Cardputer).
//
// All domain logic, state transitions, WiFi aggregation, theming, layout, and session
// coordination live in `core/include/zd/`. Pixel drawing lives in `render/include/zd/render/`.
// This file implements thin M5Unified + Arduino ESP32 HAL adapters at the boundary.

#include "zd/hal.hpp"
#include "zd/render/painter.hpp"
#include "esp32_hal.hpp"

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_system.h>

namespace {

using zd::esp32_common::Esp32WifiScanner;
using zd::esp32_common::NvsThemeStore;

// --- Thin Hardware Adapters Implementing `zd::hal` Contracts ---

class M5PowerSensor final : public zd::hal::IPowerSensor {
public:
    zd::hal::PowerStatus readPower() override {
        return zd::hal::PowerStatus{
            M5.Power.getBatteryLevel(),
            M5.Power.isCharging() == m5::Power_Class::is_charging
        };
    }
};

class M5DisplaySink final : public zd::hal::IDisplaySink {
public:
    void init() {
        M5.Display.setRotation(1);
        M5.Display.setBrightness(180);
        canvas_.setColorDepth(16);
        canvasReady_ = canvas_.createSprite(M5.Display.width(), M5.Display.height());
    }

    zd::ScreenDimensions dimensions() const override {
        return zd::ScreenDimensions{
            static_cast<int16_t>(M5.Display.width()),
            static_cast<int16_t>(M5.Display.height())
        };
    }

    void present(const zd::UiFrame& frame) override {
        // Sprite-only by design: `paintUiFrame<Gfx>` is a template, so a direct-to-display
        // fallback branch would instantiate the entire render pipeline a second time (one
        // full copy per Gfx type), just to cover an allocation failure that essentially
        // never happens in practice (the sprite is at most 320x240x2 = 150KB against
        // ~320KB of usable RAM). If it ever does fail, skip the frame rather than pay for
        // that duplication permanently in flash.
        if (!canvasReady_) {
            return;
        }
        canvas_.setFont(&fonts::Font0);
        canvas_.setTextSize(1);
        zd::render::paintUiFrame(canvas_, frame);
        canvas_.pushSprite(0, 0);
    }

private:
    M5Canvas canvas_{&M5.Display};
    bool canvasReady_ = false;
};

// --- Runtime State ---

Esp32WifiScanner gRadio;
M5PowerSensor gPower;
NvsThemeStore gThemeStore;
M5DisplaySink gDisplay;
zd::hal::PlatformSession gSession;

bool gBtnBLongHandled = false;
uint32_t gLastRenderMs = 0;
std::string gSerialLine;

void logSerialStatus(const char* eventTag) {
    const zd::AppState& st = gSession.state();
    const zd::FaceDescriptor face = zd::faceForMood(st.mood);
    Serial.printf("[%s] %s %s> \"%s\" | APs:%u | BusiestCh:%d | Theme:%s\n",
                  eventTag,
                  face.asciiFace,
                  st.petName.c_str(),
                  st.phrase.c_str(),
                  static_cast<unsigned>(st.latestScan.totalAps),
                  static_cast<int>(st.latestScan.busiestChannel),
                  zd::themeSpec(st.theme).key);
}

void pollSerialCli(uint32_t nowMs) {
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (!gSerialLine.empty()) {
                const zd::hal::CliOutcome res = zd::hal::interpretCliLine(
                    gSerialLine, gSession.state(), gDisplay.dimensions(), nowMs);
                if (!res.output.empty()) {
                    Serial.print(res.output.c_str());
                }
                if (res.event.has_value()) {
                    gSession.dispatch(*res.event, &gThemeStore);
                    logSerialStatus("SERIAL");
                }
                gSerialLine.clear();
            }
        } else if (gSerialLine.size() < 64u) {
            gSerialLine.push_back(c);
        }
    }
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    gDisplay.init();
    gRadio.init();
    gThemeStore.init();

    // Seed pure functional PRNG with hardware entropy & restore saved theme from NVS
    gSession.boot(esp_random(), millis(), &gThemeStore);
    gSession.renderTo(gDisplay);
    logSerialStatus("BOOT");
}

void loop() {
    M5.update();
    const uint32_t nowMs = millis();

    // 1. Button A -> next screen page
    if (M5.BtnA.wasPressed()) {
        gSession.dispatch(zd::ButtonAPressedEvent{nowMs}, &gThemeStore);
        logSerialStatus("BTN_A");
    }

    // 2. Button B -> short press triggers scan, 700ms hold cycles to next theme
    if (M5.BtnB.wasPressed()) {
        gBtnBLongHandled = false;
    }
    if (M5.BtnB.isPressed() && !gBtnBLongHandled && M5.BtnB.pressedFor(700)) {
        gBtnBLongHandled = true;
        gSession.dispatch(zd::CycleThemeEvent{nowMs, +1}, &gThemeStore);
        logSerialStatus("THEME");
    }
    if (M5.BtnB.wasReleased() && !gBtnBLongHandled) {
        gSession.dispatch(zd::ButtonBPressedEvent{nowMs}, &gThemeStore);
        logSerialStatus("BTN_B");
    }

    // 3. Extra hardware buttons -> previous theme
    //    M5StickC / StickC Plus: short click of PMIC power button.
    //    3-button M5Stack Core / Core2 / CoreInk / Paper: Button C.
    if (M5.BtnPWR.wasClicked() || M5.BtnC.wasPressed()) {
        gSession.dispatch(zd::CycleThemeEvent{nowMs, -1}, &gThemeStore);
        logSerialStatus("THEME");
    }

    // 4. Interactive Serial CLI ("theme neon", "theme prev", "themes", "view", "scan", "ascii")
    pollSerialCli(nowMs);

    // 5. Step non-blocking radio scanner & periodic telemetry tick
    if (const char* scanTag = gSession.stepRadio(nowMs, gRadio)) {
        logSerialStatus(scanTag);
    }
    gSession.stepTick(nowMs, gPower);

    // 6. Refresh display at ~20 FPS (50ms frame pacing)
    if ((nowMs - gLastRenderMs) >= 50u) {
        gLastRenderMs = nowMs;
        gSession.renderTo(gDisplay);
    }

    delay(15);
}
