#pragma once

// AppController: the single imperative boundary shared by the CLI/TUI and the Qt GUI.
//
// It owns the pet session (portable UI core) and the security session (offensive/defensive
// engine), plus the hybrid Linux backend, and exposes a small synchronous API the UIs call
// on a timer. All policy (mood, layout, defensive detection, the transmit authorization
// gate) stays in the pure cores; this file only sequences hardware I/O and scheduling.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "zd/hal.hpp"
#include "zd/secops_hal.hpp"
#include "zd/ui_model.hpp"

#include "../backend/cli_backend.hpp"
#include "async_job.hpp"
#include "clock.hpp"

#ifdef ZD_WITH_LIBPCAP
#include "../backend/native_backend.hpp"
#endif

namespace zd {
namespace linux_app {

enum class CaptureEngine { None, Native, Cli };

struct ActionResult {
    bool ok = false;
    std::string message;
};

// Monitor start/stop and deauth all shell out to commands that can block for a while
// (airmon-ng, aireplay-ng); all three run through one single-flight AsyncJob so the UI
// thread is never stuck waiting, and so they can never race each other.
enum class AsyncActionKind { StartMonitor, StopMonitor, Deauth };

struct AsyncActionOutcome {
    AsyncActionKind kind = AsyncActionKind::StartMonitor;
    ActionResult result;
    std::string monitorIface;  // StartMonitor only: resolved interface, or "" on failure
};

class AppController {
public:
    explicit AppController(std::string iface = "wlan0") : iface_(std::move(iface)) {}

    void boot(uint32_t seed = 0xC0FFEEu) {
        pet_.boot(seed, nowMs(), &themeStore_);
    }

    // --- Per-tick drive (call ~10 Hz from the UI) ---
    void step() {
        pollAsyncJobs();
        const uint32_t now = nowMs();
        const char* tag = pet_.stepRadio(now, scanner_);
        if (tag != nullptr && std::string(tag) == "SCAN_DONE") {
            feedScanIntoSecurity(now);
        }
        if (monitoring_ && capture_ && capture_->running()) {
            for (const sec::CapturedFrame& f : capture_->poll()) {
                sec_.ingest(f);
            }
        }
        pet_.stepTick(now, power_);
    }

    // --- Recon (active scan via NetworkManager; no monitor mode needed) ---
    void requestScan() { pet_.dispatch(ButtonBPressedEvent{nowMs()}, &themeStore_); }

    // --- Monitor mode lifecycle (needed for capture + injection) ---
    // airmon-ng commonly takes seconds, so both of these only *kick off* a worker-thread
    // job and return immediately; the real outcome surfaces later via takeLastAsyncResult()
    // once pollAsyncJobs() (called every step()) collects it.
    ActionResult startMonitor() {
        if (monitoring_) {
            return {true, "already monitoring on " + monitor_.monitorIface()};
        }
        if (actionJob_.running()) {
            return {false, "an action is already in progress"};
        }
        pendingKind_ = AsyncActionKind::StartMonitor;
        actionJob_.start([iface = iface_]() {
            const std::string mon = linux_backend::CliMonitorControl::bringUpMonitor(iface);
            const bool ok = !mon.empty();
            ActionResult res{ok, ok ? ("monitor interface " + mon + " is up")
                                    : ("failed to enable monitor mode on " + iface +
                                       " (need root / supported adapter)")};
            return AsyncActionOutcome{AsyncActionKind::StartMonitor, res, mon};
        });
        return {true, "starting monitor mode on " + iface_ + "..."};
    }

    ActionResult stopMonitor() {
        if (!monitoring_) {
            return {true, "not monitoring"};
        }
        if (actionJob_.running()) {
            return {false, "an action is already in progress"};
        }
        // Flip state synchronously, before the worker thread touches anything: step()
        // must never call capture_->poll() again once monitoring_ is false, and moving
        // capture_/injector_ out gives the worker thread exclusive, uncontended ownership.
        monitoring_ = false;
        const std::string monIface = monitor_.monitorIface();
        monitor_.applyDown();
        std::unique_ptr<sec::ICaptureSource> cap = std::move(capture_);
        injector_.reset();
        engine_ = CaptureEngine::None;
        pendingKind_ = AsyncActionKind::StopMonitor;
        actionJob_.start([cap = std::move(cap), monIface]() mutable {
            if (cap) {
                cap->stop();
            }
            linux_backend::CliMonitorControl::takeDownMonitor(monIface);
            return AsyncActionOutcome{AsyncActionKind::StopMonitor,
                                      {true, "monitor mode stopped"}, {}};
        });
        return {true, "stopping monitor mode..."};
    }

    ActionResult setChannel(int channel) {
        if (!monitoring_) {
            return {false, "not in monitor mode"};
        }
        const bool ok = monitor_.setChannel(channel);
        return {ok, ok ? ("channel " + std::to_string(channel)) : "failed to set channel"};
    }

    // --- Authorization (operator-only; drives the transmit gate) ---
    void confirmAuthorization(bool on) { sec_.authorization().operatorConfirmed = on; }
    void allowBssid(const std::string& bssid) { sec_.authorization().allow(bssid); }
    void setAllowAll(bool on) { sec_.authorization().allowAll = on; }
    const sec::Authorization& authorization() const { return sec_.authorization(); }

    // --- The one offensive transmit entry point (gate enforced inside SecSession) ---
    // The gate check + state/audit recording (beginDeauth) is fast and runs synchronously
    // right here; only the actual transmit (aireplay-ng, or a bounded pcap_sendpacket loop
    // for large counts) runs on a worker thread, since that can take a while.
    ActionResult requestDeauth(const std::string& bssid, const std::string& client,
                               uint32_t count) {
        if (!injector_ || !injector_->available()) {
            return {false, "no injector available (start monitor mode first)"};
        }
        if (!sec::isValidMac(bssid)) {
            return {false, "invalid target BSSID '" + bssid + "'"};
        }
        if (!client.empty() && !sec::isValidMac(client)) {
            return {false, "invalid client MAC '" + client + "'"};
        }
        if (actionJob_.running()) {
            return {false, "an action is already in progress"};
        }
        const sec::AuthDecision decision = sec_.beginDeauth(bssid, client, count, nowMs(), &audit_);
        if (decision != sec::AuthDecision::Allowed) {
            return {false, std::string("deauth refused: ") + sec::authDecisionLabel(decision)};
        }
        sec::IInjector* injector = injector_.get();
        pendingKind_ = AsyncActionKind::Deauth;
        actionJob_.start([injector, bssid, client, count]() {
            const sec::InjectResult r = injector->deauth(bssid, client, count);
            return AsyncActionOutcome{AsyncActionKind::Deauth, {r.transmitted, r.detail}, {}};
        });
        return {true, "transmitting " + std::to_string(count) + " deauth frame(s)..."};
    }

    // --- Async action status (polled by the UI) ---
    bool asyncBusy() const { return actionJob_.running(); }

    // Which kind of action is currently in flight, if any. monitoring() alone can't tell
    // a UI "starting" apart from "stopping" here, since stopMonitor() flips it to false
    // immediately (see its comment) rather than waiting for the worker thread.
    std::optional<AsyncActionKind> pendingActionKind() const {
        return actionJob_.running() ? std::optional<AsyncActionKind>(pendingKind_) : std::nullopt;
    }

    // Consumes (returns-and-clears) the most recently completed monitor/deauth result, or
    // nullopt if nothing new has finished since the last call.
    std::optional<ActionResult> takeLastAsyncResult() {
        std::optional<ActionResult> out = std::move(lastAsyncResult_);
        lastAsyncResult_.reset();
        return out;
    }

    // --- Pet UI event passthrough ---
    void dispatchPet(const AppEvent& ev) { pet_.dispatch(ev, &themeStore_); }

    // --- Read-only views for the UIs ---
    UiFrame petFrame(ScreenDimensions dims) const { return pet_.buildFrame(dims); }
    const AppState& petState() const { return pet_.state(); }
    const sec::SecState& secState() const { return sec_.state(); }
    const std::vector<linux_backend::ScannedAp>& lastScan() const { return scanner_.detailed(); }

    bool monitoring() const { return monitoring_; }
    std::string monitorIface() const { return monitor_.monitorIface(); }
    std::string interfaceName() const { return iface_; }
    std::string engineLabel() const {
        switch (engine_) {
            case CaptureEngine::Native: return "libpcap (native frame capture)";
            case CaptureEngine::Cli:    return "airodump-ng (CLI)";
            case CaptureEngine::None:   break;
        }
        return "none";
    }
    std::string auditPath() const { return audit_.path(); }

private:
    // UI-thread-only: collects a finished monitor/deauth job and commits whatever state
    // change it implies. Called at the top of every step().
    void pollAsyncJobs() {
        std::optional<AsyncActionOutcome> outcome = actionJob_.collect();
        if (!outcome.has_value()) {
            return;
        }
        if (outcome->kind == AsyncActionKind::StartMonitor) {
            monitor_.applyUp(outcome->monitorIface);
            if (!outcome->monitorIface.empty() && monitor_.isMonitor()) {
                buildCaptureAndInjector();
                if (capture_ && capture_->start(outcome->monitorIface)) {
                    monitoring_ = true;
                } else {
                    // Rare failure path (monitor mode came up but capture didn't): same
                    // brief airmon-ng-stop blocking call the old synchronous code made
                    // here too, just reached via a different path.
                    linux_backend::CliMonitorControl::takeDownMonitor(outcome->monitorIface);
                    monitor_.applyDown();
                    capture_.reset();
                    injector_.reset();
                    engine_ = CaptureEngine::None;
                    outcome->result = {false, "monitor up but capture failed to start on " +
                                                   outcome->monitorIface};
                }
            } else {
                monitoring_ = false;
            }
        }
        // StopMonitor already applied its state transition synchronously in stopMonitor(),
        // and Deauth's gate/state update already ran synchronously in beginDeauth() before
        // the job was started — neither needs further commit here.
        lastAsyncResult_ = outcome->result;
    }

    void feedScanIntoSecurity(uint32_t now) {
        for (const linux_backend::ScannedAp& ap : scanner_.detailed()) {
            sec_.dispatch(sec::ApObserved{now, ap.bssid, ap.ssid, ap.channel, ap.rssi, ap.enc});
        }
    }

    void buildCaptureAndInjector() {
#ifdef ZD_WITH_LIBPCAP
        capture_ = std::make_unique<linux_backend::NativeCaptureSource>();
        injector_ = std::make_unique<linux_backend::NativeInjector>(&monitor_);
        engine_ = CaptureEngine::Native;
#else
        capture_ = std::make_unique<linux_backend::CliCaptureSource>();
        injector_ = std::make_unique<linux_backend::CliInjector>(&monitor_);
        engine_ = CaptureEngine::Cli;
#endif
    }

    std::string iface_;
    hal::PlatformSession pet_;
    sec::SecSession sec_;
    linux_backend::CliMonitorControl monitor_;
    linux_backend::CliRadioScanner scanner_{iface_};
    linux_backend::SysfsPowerSensor power_;
    linux_backend::FileThemeStore themeStore_;
    linux_backend::FileAuditLog audit_;
    std::unique_ptr<sec::ICaptureSource> capture_;
    std::unique_ptr<sec::IInjector> injector_;
    CaptureEngine engine_ = CaptureEngine::None;
    bool monitoring_ = false;

    // Declared last so it is destroyed *first*: AsyncJob's destructor joins its worker
    // thread, which must happen before capture_/injector_ (which that thread may be using)
    // are torn down.
    linux_app::AsyncJob<AsyncActionOutcome> actionJob_;
    AsyncActionKind pendingKind_ = AsyncActionKind::StartMonitor;
    std::optional<ActionResult> lastAsyncResult_;
};

}  // namespace linux_app
}  // namespace zd
