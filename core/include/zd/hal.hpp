#pragma once

// Portable Hardware Abstraction Layer (HAL) & Session Coordinator.
//
// Zero OS or SDK headers live here. A new hardware target (ESP32 board, RP2040 Pico W,
// Linux SBC + e-Paper HAT, desktop app) implements the tiny adapter interfaces below —
// or uses the standalone value types directly — while `PlatformSession` handles boot
// theme restoration, flash-wear-safe theme persistence, async scan scheduling/timeouts,
// CLI command parsing, and frame projection.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "domain.hpp"
#include "pet_logic.hpp"
#include "theme.hpp"
#include "ui_model.hpp"

namespace zd {
namespace hal {

struct PowerStatus {
    int32_t batteryPercent = 100;
    bool isCharging = false;
};

enum class ScanPollState : uint8_t {
    Running = 0,
    Completed,
    Failed
};

struct ScanPollResult {
    ScanPollState state = ScanPollState::Running;
    std::vector<AccessPoint> networks{};
};

// --- Optional abstract adapter contracts for multi-device ports ----------------------

class IRadioScanner {
public:
    virtual ~IRadioScanner() = default;
    // Kick off a non-blocking 2.4 GHz scan. Return true if accepted/started.
    virtual bool startAsyncScan() = 0;
    // Poll an active scan without blocking the UI loop.
    virtual ScanPollResult pollScan() = 0;
    // Release any driver-held scan buffers after completion/timeout.
    virtual void clearScan() {}
};

class IPowerSensor {
public:
    virtual ~IPowerSensor() = default;
    virtual PowerStatus readPower() = 0;
};

class IThemeStore {
public:
    virtual ~IThemeStore() = default;
    // Return persisted theme slug (e.g. "neon") or empty string if unset.
    virtual std::string loadThemeKey() = 0;
    // Persist stable theme slug (e.g. "operator") to NVS / file / EEPROM.
    virtual void saveThemeKey(std::string_view key) = 0;
};

class IDisplaySink {
public:
    virtual ~IDisplaySink() = default;
    virtual ScreenDimensions dimensions() const = 0;
    virtual void present(const UiFrame& frame) = 0;
};

// Null object defaults for targets without battery telemetry or persistent storage.
class FixedPowerSensor final : public IPowerSensor {
public:
    explicit FixedPowerSensor(int32_t pct = 100, bool charging = false)
        : status_{pct, charging} {}
    PowerStatus readPower() override { return status_; }

private:
    PowerStatus status_;
};

class MemoryThemeStore final : public IThemeStore {
public:
    explicit MemoryThemeStore(std::string initialKey = {}) : key_(std::move(initialKey)) {}
    std::string loadThemeKey() override { return key_; }
    void saveThemeKey(std::string_view key) override { key_.assign(key.data(), key.size()); }
    const std::string& storedKey() const { return key_; }

private:
    std::string key_;
};

// --- Shared portable text CLI (Serial Monitor, desktop terminal, SSH, USB CDC) -------

struct CliOutcome {
    std::optional<AppEvent> event{};
    std::string output{};
    bool requestQuit = false;
};

inline std::string_view trimAscii(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

inline std::string formatThemeTable(ThemeId active) {
    std::string out = "Available themes (theme <key|next|prev>):\n";
    for (const ThemeSpec& t : kThemeCatalog) {
        const std::string keyCol(t.key);
        const std::string nameCol(t.name);
        out += (t.id == active) ? " * " : "   ";
        out += keyCol + std::string(keyCol.size() < 10u ? 10u - keyCol.size() : 1u, ' ');
        out += nameCol + std::string(nameCol.size() < 13u ? 13u - nameCol.size() : 1u, ' ');
        out += t.blurb;
        out += "\n";
    }
    return out;
}

// Pure command interpreter usable across Serial, stdin, WebSocket, or keyboard consoles.
inline CliOutcome interpretCliLine(std::string_view rawLine,
                                   const AppState& state,
                                   ScreenDimensions dims,
                                   uint32_t nowMs) {
    CliOutcome out{};
    const std::string_view line = trimAscii(rawLine);
    if (line.empty()) {
        return out;
    }

    const std::size_t sp = line.find(' ');
    const std::string_view cmd = line.substr(0, sp);
    const std::string_view arg =
        (sp == std::string_view::npos) ? std::string_view{} : trimAscii(line.substr(sp + 1));

    if (cmd == "q" || cmd == "quit" || cmd == "exit") {
        out.requestQuit = true;
    } else if (cmd == "themes") {
        out.output = formatThemeTable(state.theme);
    } else if (cmd == "t") {
        out.event = CycleThemeEvent{nowMs, +1};
    } else if (cmd == "T") {
        out.event = CycleThemeEvent{nowMs, -1};
    } else if (cmd == "theme") {
        if (arg.empty() || arg == "next" || arg == "+") {
            out.event = CycleThemeEvent{nowMs, +1};
        } else if (arg == "prev" || arg == "back" || arg == "-") {
            out.event = CycleThemeEvent{nowMs, -1};
        } else if (const auto theme = themeFromKey(arg)) {
            out.event = SetThemeEvent{nowMs, *theme, true};
        } else {
            out.output = "Unknown theme '" + std::string(arg) + "'.\n" + formatThemeTable(state.theme);
        }
    } else if (cmd == "a" || cmd == "view" || cmd == "page") {
        if (arg == "prev" || arg == "back" || arg == "-") {
            out.event = CyclePageEvent{nowMs, -1};
        } else {
            out.event = CyclePageEvent{nowMs, +1};
        }
    } else if (cmd == "b" || cmd == "scan") {
        out.event = ButtonBPressedEvent{nowMs};
    } else if (cmd == "ascii") {
        out.output = renderAsciiPreview(buildUiFrame(state, dims));
    } else if (cmd == "status") {
        const FaceDescriptor f = faceForMood(state.mood);
        out.output = std::string(f.asciiFace) + " " + state.petName + "> \"" + state.phrase +
                     "\" | APs:" + std::to_string(state.latestScan.totalAps) +
                     " | Theme:" + themeSpec(state.theme).key + "\n";
    } else {
        out.output =
            "Commands: theme [next|prev|<key>] | themes | view [next|prev] | scan | ascii | status\n";
    }
    return out;
}

// --- Reusable imperative-boundary coordinator ----------------------------------------

class PlatformSession {
public:
    static constexpr uint32_t kDefaultScanTimeoutMs = 15000u;

    void boot(uint32_t bootSeed, uint32_t nowMs, IThemeStore* store = nullptr) {
        state_ = initialState(bootSeed);
        if (store != nullptr) {
            const std::string saved = store->loadThemeKey();
            if (const auto theme = themeFromKey(saved)) {
                state_ = reduce(state_, SetThemeEvent{nowMs, *theme, /*announce=*/false});
            }
        }
        persistedTheme_ = state_.theme;
    }

    void dispatch(const AppEvent& ev, IThemeStore* store = nullptr) {
        state_ = reduce(state_, ev);
        syncThemeStore(store);
    }

    // Drives non-blocking scan start, completion, and timeout recovery.
    // Returns a diagnostic tag ("SCAN_START", "SCAN_DONE", "SCAN_EMPTY") when a transition occurs.
    const char* stepRadio(uint32_t nowMs,
                          IRadioScanner& radio,
                          uint32_t scanTimeoutMs = kDefaultScanTimeoutMs) {
        const char* tag = nullptr;
        if (state_.isScanning) {
            const ScanPollResult poll = radio.pollScan();
            if (poll.state == ScanPollState::Completed) {
                radio.clearScan();
                state_ = reduce(state_, ScanCompletedEvent{nowMs, poll.networks});
                tag = "SCAN_DONE";
            } else if (poll.state == ScanPollState::Failed ||
                       (nowMs - state_.lastScanStartMs) > scanTimeoutMs) {
                radio.clearScan();
                state_ = reduce(state_, ScanCompletedEvent{nowMs, {}});
                tag = "SCAN_EMPTY";
            }
        }

        if (shouldTriggerScan(state_, nowMs)) {
            radio.clearScan();
            if (radio.startAsyncScan()) {
                state_ = reduce(state_, ScanStartedEvent{nowMs});
                if (tag == nullptr) {
                    tag = "SCAN_START";
                }
            }
        }
        return tag;
    }

    void stepTick(uint32_t nowMs, IPowerSensor& power) {
        const PowerStatus p = power.readPower();
        state_ = reduce(state_, TickEvent{nowMs, p.batteryPercent, p.isCharging});
    }

    UiFrame buildFrame(ScreenDimensions dims) const {
        return buildUiFrame(state_, dims);
    }

    void renderTo(IDisplaySink& sink) const {
        sink.present(buildFrame(sink.dimensions()));
    }

    const AppState& state() const { return state_; }

private:
    void syncThemeStore(IThemeStore* store) {
        if (store == nullptr || state_.theme == persistedTheme_) {
            return;
        }
        store->saveThemeKey(themeSpec(state_.theme).key);
        persistedTheme_ = state_.theme;
    }

    AppState state_ = initialState();
    ThemeId persistedTheme_ = ThemeId::PaperInk;
};

}  // namespace hal
}  // namespace zd
