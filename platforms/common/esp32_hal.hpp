#pragma once

// Shared Arduino-ESP32 Hardware Adapters for `zd::hal` contracts.
//
// Used across all ESP32 / ESP32-S3 targets (M5StickC, M5StickC Plus/Plus2, M5Stack Core2,
// LILYGO T-Deck / T-Deck Plus) so radio scanning and NVS flash persistence live in one place.

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "zd/hal.hpp"

namespace zd {
namespace esp32_common {

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

}  // namespace esp32_common
}  // namespace zd
