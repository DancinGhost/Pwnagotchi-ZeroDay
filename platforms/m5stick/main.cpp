// Imperative Hardware Shell for pwnagotchi-zeroday (M5StickC / Plus / Plus2, and other
// M5Unified-supported ESP32 boards such as M5Stack Core / Core2 / Cardputer).
//
// All domain logic, state transitions, WiFi aggregation, theming, layout, and session
// coordination live in `core/include/zd/`. Pixel drawing lives in `render/include/zd/render/`.
// This file implements thin M5Unified + Arduino ESP32 HAL adapters at the boundary.

#include "zd/hal.hpp"
#include "zd/render/painter.hpp"

#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>

namespace {

// --- Thin Hardware Adapters Implementing `zd::hal` Contracts ---

class Esp32WifiScanner final : public zd::hal::IRadioScanner {
public:
    void init() {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true, true);
    }

    bool startAsyncScan() override {
        const int16_t rc = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true);
        return (rc == WIFI_SCAN_RUNNING || rc >= 0);
    }

    zd::hal::ScanPollResult pollScan() override {
        const int16_t status = WiFi.scanComplete();
        if (status == WIFI_SCAN_RUNNING) {
            return {zd::hal::ScanPollState::Running, {}};
        }
        if (status < 0) {
            return {zd::hal::ScanPollState::Failed, {}};
        }
        std::vector<zd::AccessPoint> networks;
        networks.reserve(static_cast<std::size_t>(status));
        for (int16_t i = 0; i < status; ++i) {
            networks.push_back(zd::AccessPoint{
                std::string(WiFi.SSID(i).c_str()),
                static_cast<int32_t>(WiFi.RSSI(i)),
                static_cast<int32_t>(WiFi.channel(i)),
                WiFi.encryptionType(i) != WIFI_AUTH_OPEN
            });
        }
        return {zd::hal::ScanPollState::Completed, std::move(networks)};
    }

    void clearScan() override {
        WiFi.scanDelete();
    }
};

class M5PowerSensor final : public zd::hal::IPowerSensor {
public:
    zd::hal::PowerStatus readPower() override {
        return zd::hal::PowerStatus{
            M5.Power.getBatteryLevel(),
            M5.Power.isCharging() == m5::Power_Class::is_charging
        };
    }
};

class NvsThemeStore final : public zd::hal::IThemeStore {
public:
    void init() {
        ready_ = prefs_.begin("zeroday", /*readOnly=*/false);
    }

    std::string loadThemeKey() override {
        if (!ready_) {
            return {};
        }
        return std::string(prefs_.getString("theme", "").c_str());
    }

    void saveThemeKey(std::string_view key) override {
        if (!ready_) {
            return;
        }
        const std::string owned(key);
        prefs_.putString("theme", owned.c_str());
    }

private:
    Preferences prefs_;
    bool ready_ = false;
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
        if (canvasReady_) {
            canvas_.setFont(&fonts::Font0);
            canvas_.setTextSize(1);
            zd::render::paintUiFrame(canvas_, frame);
            canvas_.pushSprite(0, 0);
        } else {
            M5.Display.setFont(&fonts::Font0);
            M5.Display.setTextSize(1);
            zd::render::paintUiFrame(M5.Display, frame);
        }
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
