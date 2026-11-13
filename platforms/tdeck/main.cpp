// Imperative Hardware Shell for pwnagotchi-zeroday on LILYGO T-Deck / T-Deck Plus.
//
// Hardware Features:
// - ESP32-S3FN16R8 (Dual-core LX7 @ 240MHz, 16MB Flash, 8MB OPI PSRAM)
// - 2.8" IPS ST7789 Display (320x240, Landscape QVGA wide layout)
// - GT911 Capacitive Touchscreen (T-Deck Plus)
// - Trackball Navigation (Up/Down/Left/Right/Center Click on GPIO 0/BOOT)
// - ESP32-C3 QWERTY Keyboard Coprocessor (I2C 0x55, INT GPIO 46)
// - Battery ADC Voltage Divider (GPIO 4 with 2:1 divider)
// - Peripheral Power Rail Gate (GPIO 10, required for battery operation)
// - MIA-M10Q GNSS / GPS Module (T-Deck Plus, RX 44, TX 43)
// - SX1262 LoRa (CS GPIO 9) & MicroSD (CS GPIO 39) SPI bus isolation
//
// All domain logic, state transitions, WiFi aggregation, theming, layout, and session
// coordination live in `core/include/zd/`. Pixel drawing lives in `render/include/zd/render/`.

#include "zd/hal.hpp"
#include "zd/render/painter.hpp"

#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>
#include <algorithm>
#include <cstring>
#include <esp_random.h>
#include <string_view>

namespace {

// --- Pinout & Hardware Constants for LILYGO T-Deck / T-Deck Plus ---

constexpr uint8_t PIN_POWER_ON   = 10; // High enables peripheral power rail on battery
constexpr uint8_t PIN_LORA_CS    = 9;  // Shared SPI CS: pull HIGH to avoid bus contention
constexpr uint8_t PIN_SD_CS      = 39; // Shared SPI CS: pull HIGH to avoid bus contention
constexpr uint8_t PIN_BAT_ADC    = 4;  // Battery voltage divider (2:1 ratio)

// Trackball pins
constexpr uint8_t PIN_TB_CLICK   = 0;  // Center click / ESP32-S3 BOOT button (active LOW)
constexpr uint8_t PIN_TB_UP      = 3;  // Trackball Up
constexpr uint8_t PIN_TB_DOWN    = 15; // Trackball Down
constexpr uint8_t PIN_TB_LEFT    = 1;  // Trackball Left
constexpr uint8_t PIN_TB_RIGHT   = 2;  // Trackball Right

// I2C bus pins (shared by ESP32-C3 keyboard coprocessor & GT911 touchscreen)
constexpr uint8_t PIN_I2C_SDA    = 18;
constexpr uint8_t PIN_I2C_SCL    = 8;
constexpr uint8_t PIN_KB_INT     = 46; // Active LOW when keyboard key is ready
constexpr uint8_t KB_I2C_ADDR    = 0x55;

// ST7789 Display SPI pins
constexpr uint8_t PIN_LCD_SCLK   = 40;
constexpr uint8_t PIN_LCD_MOSI   = 41;
constexpr uint8_t PIN_LCD_MISO   = 38;
constexpr uint8_t PIN_LCD_DC     = 11;
constexpr uint8_t PIN_LCD_CS     = 12;
constexpr uint8_t PIN_LCD_BL     = 42;

// GT911 Touchscreen pins (T-Deck Plus)
constexpr uint8_t PIN_TOUCH_INT  = 16;
constexpr uint8_t TOUCH_I2C_ADDR = 0x5D;

// MIA-M10Q GNSS / GPS UART pins (T-Deck Plus)
constexpr uint8_t PIN_GPS_RX     = 44; // ESP32 RX <- GPS TX
constexpr uint8_t PIN_GPS_TX     = 43; // ESP32 TX -> GPS RX

// --- LovyanGFX Hardware Device Driver for T-Deck / T-Deck Plus ---

class LGFX_TDeck : public lgfx::LGFX_Device {
    lgfx::Bus_SPI     bus_instance_;
    lgfx::Light_PWM   light_instance_;
    lgfx::Panel_ST7789 panel_instance_;
    lgfx::Touch_GT911  touch_instance_;

public:
    LGFX_TDeck() {
        // SPI Bus configuration
        {
            auto cfg = bus_instance_.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.spi_3wire  = true;
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.pin_sclk   = PIN_LCD_SCLK;
            cfg.pin_mosi   = PIN_LCD_MOSI;
            cfg.pin_miso   = PIN_LCD_MISO;
            cfg.pin_dc     = PIN_LCD_DC;
            bus_instance_.config(cfg);
            panel_instance_.setBus(&bus_instance_);
        }

        // Backlight PWM configuration
        {
            auto cfg = light_instance_.config();
            cfg.pin_bl      = PIN_LCD_BL;
            cfg.freq        = 12000;
            cfg.pwm_channel = 7;
            cfg.invert      = false;
            light_instance_.config(cfg);
            panel_instance_.light(&light_instance_);
        }

        // ST7789 Panel configuration
        {
            auto cfg = panel_instance_.config();
            cfg.pin_cs       = PIN_LCD_CS;
            cfg.pin_rst      = -1;
            cfg.panel_width  = 240;
            cfg.panel_height = 320;
            cfg.offset_x     = 0;
            cfg.offset_y     = 0;
            cfg.invert       = true;
            cfg.rgb_order    = false;
            cfg.dlen_16bit   = false;
            cfg.bus_shared   = true;
            panel_instance_.config(cfg);
        }

        // GT911 Capacitive Touch configuration (T-Deck Plus)
        {
            auto cfg = touch_instance_.config();
            cfg.x_min      = 0;
            cfg.x_max      = 319;
            cfg.y_min      = 0;
            cfg.y_max      = 239;
            cfg.pin_int    = PIN_TOUCH_INT;
            cfg.pin_rst    = -1;
            cfg.bus_shared = true;
            cfg.i2c_port   = 0;
            cfg.i2c_addr   = TOUCH_I2C_ADDR;
            cfg.pin_sda    = PIN_I2C_SDA;
            cfg.pin_scl    = PIN_I2C_SCL;
            cfg.freq       = 400000;
            touch_instance_.config(cfg);
            panel_instance_.setTouch(&touch_instance_);
        }

        setPanel(&panel_instance_);
    }
};

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

class TDeckPowerSensor final : public zd::hal::IPowerSensor {
public:
    void init() {
        pinMode(PIN_BAT_ADC, INPUT);
        analogSetAttenuation(ADC_11db);
    }

    zd::hal::PowerStatus readPower() override {
        // Sample ADC with 4-reading moving average
        uint32_t sumMv = 0;
        for (int i = 0; i < 4; ++i) {
            sumMv += analogReadMilliVolts(PIN_BAT_ADC);
        }
        const uint32_t rawMv = sumMv / 4;
        // Hardware features a 2:1 resistor divider (two 100k resistors)
        const uint32_t batMv = rawMv * 2;

        // LiPo curve: 4200 mV (100%), 3300 mV (0%)
        int32_t pct = 0;
        if (batMv >= 4200) {
            pct = 100;
        } else if (batMv <= 3300) {
            pct = 0;
        } else {
            pct = static_cast<int32_t>((batMv - 3300) * 100 / 900);
        }

        // Elevated voltage when USB charging (>4.25V)
        const bool charging = (batMv > 4250);
        return zd::hal::PowerStatus{pct, charging};
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

class TDeckDisplaySink final : public zd::hal::IDisplaySink {
public:
    void init() {
        // 1. Enable peripheral power rail (critical for display & peripherals on battery)
        pinMode(PIN_POWER_ON, OUTPUT);
        digitalWrite(PIN_POWER_ON, HIGH);

        // 2. Drive unused shared SPI CS pins HIGH to eliminate bus contention
        pinMode(PIN_LORA_CS, OUTPUT);
        digitalWrite(PIN_LORA_CS, HIGH);
        pinMode(PIN_SD_CS, OUTPUT);
        digitalWrite(PIN_SD_CS, HIGH);

        // 3. Initialize LCD in landscape (320x240 QVGA)
        lcd_.init();
        lcd_.setRotation(1);
        lcd_.setBrightness(180);

        // 4. Create RGB565 double-buffered sprite
        canvas_.setColorDepth(16);
        canvasReady_ = canvas_.createSprite(lcd_.width(), lcd_.height());
    }

    zd::ScreenDimensions dimensions() const override {
        return zd::ScreenDimensions{
            static_cast<int16_t>(lcd_.width()),
            static_cast<int16_t>(lcd_.height())
        };
    }

    void present(const zd::UiFrame& frame) override {
        if (!canvasReady_) {
            return;
        }
        canvas_.setFont(&fonts::Font0);
        canvas_.setTextSize(1);
        zd::render::paintUiFrame(canvas_, frame);
        canvas_.pushSprite(0, 0);
    }

    LGFX_TDeck& lcd() { return lcd_; }

private:
    LGFX_TDeck lcd_;
    lgfx::LGFX_Sprite canvas_{&lcd_};
    bool canvasReady_ = false;
};

// --- Optional MIA-M10Q GNSS / GPS Telemetry (T-Deck Plus) ---

struct GpsState {
    bool hasFix = false;
    float latitude = 0.0f;
    float longitude = 0.0f;
    uint8_t satellites = 0;
    char utcTime[10] = "00:00:00";
    uint32_t lastSentenceMs = 0;
};

HardwareSerial gGpsSerial(1);
GpsState gGps;
std::string gGpsNmeaLine;

void initGps() {
    gGpsSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
}

// Zero-heap-allocation NMEA field tokenizer using string_view to eliminate heap fragmentation
void parseNmeaField(std::string_view line, std::vector<std::string_view>& fields) {
    fields.clear();
    std::size_t start = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == ',' || line[i] == '*') {
            fields.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    fields.push_back(line.substr(start));
}

void pollGps(uint32_t nowMs) {
    while (gGpsSerial.available() > 0) {
        const char c = static_cast<char>(gGpsSerial.read());
        if (c == '\n' || c == '\r') {
            if (gGpsNmeaLine.rfind("$GNRMC", 0) == 0 || gGpsNmeaLine.rfind("$GPRMC", 0) == 0) {
                // Example: $GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
                std::vector<std::string_view> f;
                f.reserve(16);
                parseNmeaField(gGpsNmeaLine, f);
                if (f.size() >= 7 && f[2] == "A") { // Status 'A' = Valid fix
                    gGps.hasFix = true;
                    gGps.lastSentenceMs = nowMs;
                    if (f[1].size() >= 6) {
                        snprintf(gGps.utcTime, sizeof(gGps.utcTime), "%.2s:%.2s:%.2s",
                                 f[1].data(), f[1].data() + 2, f[1].data() + 4);
                    }
                    if (!f[3].empty() && !f[5].empty()) {
                        char latBuf[16] = {0};
                        const std::size_t latLen = std::min(f[3].size(), sizeof(latBuf) - 1);
                        std::memcpy(latBuf, f[3].data(), latLen);
                        const float rawLat = std::strtof(latBuf, nullptr);
                        const float latDeg = std::floor(rawLat / 100.0f);
                        float lat = latDeg + ((rawLat - (latDeg * 100.0f)) / 60.0f);
                        if (f[4] == "S") lat = -lat;
                        gGps.latitude = lat;

                        char lonBuf[16] = {0};
                        const std::size_t lonLen = std::min(f[5].size(), sizeof(lonBuf) - 1);
                        std::memcpy(lonBuf, f[5].data(), lonLen);
                        const float rawLon = std::strtof(lonBuf, nullptr);
                        const float lonDeg = std::floor(rawLon / 100.0f);
                        float lon = lonDeg + ((rawLon - (lonDeg * 100.0f)) / 60.0f);
                        if (f[6] == "W") lon = -lon;
                        gGps.longitude = lon;
                    }
                }
            } else if (gGpsNmeaLine.rfind("$GNGGA", 0) == 0 || gGpsNmeaLine.rfind("$GPGGA", 0) == 0) {
                std::vector<std::string_view> f;
                f.reserve(16);
                parseNmeaField(gGpsNmeaLine, f);
                if (f.size() >= 8 && !f[7].empty()) {
                    char satBuf[8] = {0};
                    const std::size_t satLen = std::min(f[7].size(), sizeof(satBuf) - 1);
                    std::memcpy(satBuf, f[7].data(), satLen);
                    gGps.satellites = static_cast<uint8_t>(std::atoi(satBuf));
                }
            }
            gGpsNmeaLine.clear();
        } else if (gGpsNmeaLine.size() < 120u) {
            gGpsNmeaLine.push_back(c);
        }
    }
}

// --- Runtime State & Adapters ---

Esp32WifiScanner gRadio;
TDeckPowerSensor gPower;
NvsThemeStore gThemeStore;
TDeckDisplaySink gDisplay;
zd::hal::PlatformSession gSession;

uint32_t gLastRenderMs = 0;
std::string gSerialLine;
std::string gKeyboardLine;

// Trackball button state
bool gTbClickActive = false;
bool gTbLongHandled = false;
uint32_t gTbClickStartMs = 0;
uint32_t gLastTbRollMs = 0;

// Touch button state
bool gTouchActive = false;
uint32_t gTouchStartMs = 0;
uint16_t gTouchStartX = 0;
uint16_t gTouchStartY = 0;

void logSerialStatus(const char* eventTag) {
    const zd::AppState& st = gSession.state();
    const zd::FaceDescriptor face = zd::faceForMood(st.mood);
    Serial.printf("[%s] %s %s> \"%s\" | APs:%u | BusiestCh:%d | Theme:%s",
                  eventTag,
                  face.asciiFace,
                  st.petName.c_str(),
                  st.phrase.c_str(),
                  static_cast<unsigned>(st.latestScan.totalAps),
                  static_cast<int>(st.latestScan.busiestChannel),
                  zd::themeSpec(st.theme).key);
    if (gGps.hasFix) {
        Serial.printf(" | GPS: %.4f,%.4f (%u sats)",
                      gGps.latitude, gGps.longitude, static_cast<unsigned>(gGps.satellites));
    }
    Serial.println();
}

void pollSerialCli(uint32_t nowMs) {
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (!gSerialLine.empty()) {
                if (gSerialLine == "gps") {
                    if (gGps.hasFix) {
                        Serial.printf("GPS: FIX OK | Lat: %.5f Lon: %.5f | Sats: %u | Time: %s UTC\n",
                                      gGps.latitude, gGps.longitude, gGps.satellites, gGps.utcTime);
                    } else {
                        Serial.println("GPS: Searching for satellites (no fix yet)...");
                    }
                } else {
                    const zd::hal::CliOutcome res = zd::hal::interpretCliLine(
                        gSerialLine, gSession.state(), gDisplay.dimensions(), nowMs);
                    if (!res.output.empty()) {
                        Serial.print(res.output.c_str());
                    }
                    if (res.event.has_value()) {
                        gSession.dispatch(*res.event, &gThemeStore);
                        logSerialStatus("SERIAL");
                    }
                }
                gSerialLine.clear();
            }
        } else if (gSerialLine.size() < 64u) {
            gSerialLine.push_back(c);
        }
    }
}

void initTrackball() {
    pinMode(PIN_TB_CLICK, INPUT_PULLUP);
    pinMode(PIN_TB_UP, INPUT_PULLUP);
    pinMode(PIN_TB_DOWN, INPUT_PULLUP);
    pinMode(PIN_TB_LEFT, INPUT_PULLUP);
    pinMode(PIN_TB_RIGHT, INPUT_PULLUP);
}

void pollTrackball(uint32_t nowMs) {
    // 1. Center Click / BOOT button (GPIO 0)
    const bool clickPressed = (digitalRead(PIN_TB_CLICK) == LOW);
    if (clickPressed && !gTbClickActive) {
        gTbClickActive = true;
        gTbClickStartMs = nowMs;
        gTbLongHandled = false;
    } else if (clickPressed && gTbClickActive && !gTbLongHandled) {
        // Hold trackball center click for 600ms -> Trigger WiFi scan (Button B)
        if ((nowMs - gTbClickStartMs) >= 600u) {
            gTbLongHandled = true;
            gSession.dispatch(zd::ButtonBPressedEvent{nowMs}, &gThemeStore);
            logSerialStatus("TB_HOLD_SCAN");
        }
    } else if (!clickPressed && gTbClickActive) {
        gTbClickActive = false;
        if (!gTbLongHandled) {
            // Short center click -> Cycle screen view (Button A) / wake pet
            gSession.dispatch(zd::ButtonAPressedEvent{nowMs}, &gThemeStore);
            logSerialStatus("TB_CLICK_VIEW");
        }
    }

    // 2. Directional roll (active LOW pulses with debouncing)
    constexpr uint32_t kRollDebounceMs = 140u;
    if ((nowMs - gLastTbRollMs) >= kRollDebounceMs) {
        if (digitalRead(PIN_TB_LEFT) == LOW) {
            gLastTbRollMs = nowMs;
            gSession.dispatch(zd::CycleThemeEvent{nowMs, -1}, &gThemeStore);
            logSerialStatus("TB_LEFT_THEME");
        } else if (digitalRead(PIN_TB_RIGHT) == LOW) {
            gLastTbRollMs = nowMs;
            gSession.dispatch(zd::CycleThemeEvent{nowMs, +1}, &gThemeStore);
            logSerialStatus("TB_RIGHT_THEME");
        } else if (digitalRead(PIN_TB_UP) == LOW) {
            gLastTbRollMs = nowMs;
            gSession.dispatch(zd::CyclePageEvent{nowMs, -1}, &gThemeStore);
            logSerialStatus("TB_UP_VIEW");
        } else if (digitalRead(PIN_TB_DOWN) == LOW) {
            gLastTbRollMs = nowMs;
            gSession.dispatch(zd::CyclePageEvent{nowMs, +1}, &gThemeStore);
            logSerialStatus("TB_DOWN_VIEW");
        }
    }
}

void initKeyboard() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    pinMode(PIN_KB_INT, INPUT_PULLUP);
}

void pollKeyboard(uint32_t nowMs) {
    if (digitalRead(PIN_KB_INT) == LOW) {
        Wire.requestFrom(static_cast<uint8_t>(KB_I2C_ADDR), static_cast<uint8_t>(1));
        if (Wire.available() > 0) {
            const char c = static_cast<char>(Wire.read());
            if (c == 0) {
                return;
            }

            if (c == '\r' || c == '\n') {
                if (gKeyboardLine.empty()) {
                    // Empty Enter key: quick shortcut triggers a WiFi scan
                    gSession.dispatch(zd::ButtonBPressedEvent{nowMs}, &gThemeStore);
                    logSerialStatus("KB_SCAN");
                } else {
                    if (gKeyboardLine == "gps") {
                        if (gGps.hasFix) {
                            Serial.printf("[GPS] FIX OK | Lat: %.5f Lon: %.5f | Sats: %u | Time: %s UTC\n",
                                          gGps.latitude, gGps.longitude, gGps.satellites, gGps.utcTime);
                        } else {
                            Serial.println("[GPS] Searching for satellites (no fix yet)...");
                        }
                    } else {
                        const zd::hal::CliOutcome res = zd::hal::interpretCliLine(
                            gKeyboardLine, gSession.state(), gDisplay.dimensions(), nowMs);
                        if (!res.output.empty()) {
                            Serial.print(res.output.c_str());
                        }
                        if (res.event.has_value()) {
                            gSession.dispatch(*res.event, &gThemeStore);
                            logSerialStatus("KB_CLI");
                        }
                    }
                    gKeyboardLine.clear();
                }
            } else if (c == 0x08 || c == 127) { // Backspace
                if (!gKeyboardLine.empty()) {
                    gKeyboardLine.pop_back();
                }
            } else if (c == 0x1B) { // Escape
                gKeyboardLine.clear();
            } else if (gKeyboardLine.empty() && (c == ' ' || c == '\t')) {
                // Immediate hotkey: Space or Tab cycles screen view
                gSession.dispatch(zd::CyclePageEvent{nowMs, +1}, &gThemeStore);
                logSerialStatus("KB_VIEW");
            } else if (gKeyboardLine.empty() && c == '1') {
                gSession.dispatch(zd::SetPageEvent{nowMs, zd::ScreenPage::PetFace}, &gThemeStore);
                logSerialStatus("KB_PAGE_FACE");
            } else if (gKeyboardLine.empty() && c == '2') {
                gSession.dispatch(zd::SetPageEvent{nowMs, zd::ScreenPage::NearbyRadar}, &gThemeStore);
                logSerialStatus("KB_PAGE_RADAR");
            } else if (gKeyboardLine.empty() && c == '3') {
                gSession.dispatch(zd::SetPageEvent{nowMs, zd::ScreenPage::ChannelSpectrum}, &gThemeStore);
                logSerialStatus("KB_PAGE_SPECTRUM");
            } else if (gKeyboardLine.size() < 64u && c >= 32 && c <= 126) {
                gKeyboardLine.push_back(c);
            }
        }
    }
}

void pollTouch(uint32_t nowMs, LGFX_TDeck& lcd) {
    uint16_t tx = 0, ty = 0;
    const bool isTouched = lcd.getTouch(&tx, &ty);

    if (isTouched && !gTouchActive) {
        gTouchActive = true;
        gTouchStartMs = nowMs;
        gTouchStartX = tx;
        gTouchStartY = ty;
    } else if (!isTouched && gTouchActive) {
        gTouchActive = false;
        const uint32_t duration = nowMs - gTouchStartMs;
        if (duration < 500u) {
            // Tapped top header bar -> cycle theme
            if (gTouchStartY < 42) {
                gSession.dispatch(zd::CycleThemeEvent{nowMs, +1}, &gThemeStore);
                logSerialStatus("TOUCH_THEME");
            } else if (gTouchStartX < 100) {
                // Tapped left third -> previous view
                gSession.dispatch(zd::CyclePageEvent{nowMs, -1}, &gThemeStore);
                logSerialStatus("TOUCH_VIEW_PREV");
            } else if (gTouchStartX > 220) {
                // Tapped right third -> next view
                gSession.dispatch(zd::CyclePageEvent{nowMs, +1}, &gThemeStore);
                logSerialStatus("TOUCH_VIEW_NEXT");
            } else {
                // Tapped center area -> scan / wake pet
                gSession.dispatch(zd::ButtonBPressedEvent{nowMs}, &gThemeStore);
                logSerialStatus("TOUCH_SCAN");
            }
        }
    }
}

} // namespace

void setup() {
    Serial.begin(115200);

    // Initialize display & peripheral power rails
    gDisplay.init();
    initTrackball();
    initKeyboard();
    initGps();

    gRadio.init();
    gPower.init();
    gThemeStore.init();

    // Seed pure functional PRNG with hardware entropy & restore theme from NVS
    gSession.boot(esp_random(), millis(), &gThemeStore);
    gSession.renderTo(gDisplay);
    logSerialStatus("BOOT");
}

void loop() {
    const uint32_t nowMs = millis();

    // 1. Poll physical inputs: trackball, keyboard coprocessor, touchscreen, serial CLI
    pollTrackball(nowMs);
    pollKeyboard(nowMs);
    pollTouch(nowMs, gDisplay.lcd());
    pollSerialCli(nowMs);
    pollGps(nowMs);

    // 2. Step non-blocking radio scanner & periodic telemetry tick
    if (const char* scanTag = gSession.stepRadio(nowMs, gRadio)) {
        logSerialStatus(scanTag);
    }
    gSession.stepTick(nowMs, gPower);

    // 3. Refresh display at ~20 FPS (50ms frame pacing)
    if ((nowMs - gLastRenderMs) >= 50u) {
        gLastRenderMs = nowMs;
        gSession.renderTo(gDisplay);
    }

    delay(10);
}
