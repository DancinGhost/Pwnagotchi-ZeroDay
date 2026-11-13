#pragma once

// CLI-tool realizations of the security HAL, using the standard Linux/Kali wireless
// toolchain already present on the system:
//   - monitor mode / channel : airmon-ng, iw
//   - AP + station discovery : airodump-ng (CSV dump, polled)
//   - active scan            : nmcli / iw
//   - deauth injection       : aireplay-ng   (authorization-gated upstream by SecSession)
//   - battery                : /sys/class/power_supply
//   - theme persistence      : ~/.config/pwnagotchi-zeroday/theme
//   - audit trail            : ~/.local/share/pwnagotchi-zeroday/audit.log
//
// This is the always-available fallback half of the hybrid backend; the native
// libpcap/libnl half (native_backend.hpp) is preferred when compiled in.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "zd/hal.hpp"
#include "zd/secops_hal.hpp"
#include "parsers.hpp"
#include "process.hpp"
#include "../app/async_job.hpp"
#include "../app/clock.hpp"

namespace zd {
namespace linux_backend {

inline std::string readFileToString(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline std::string homeDir() {
    const char* h = std::getenv("HOME");
    return (h != nullptr) ? std::string(h) : std::string("/tmp");
}

// Create `dir` and any missing parents (POSIX `mkdir -p`, without spawning a subprocess).
// Defaults to mode 0700 (owner-only access) for secure storage of config & audit files.
inline void makeDirRecursive(const std::string& dir, mode_t mode = 0700) {
    std::string partial;
    partial.reserve(dir.size());
    for (std::size_t i = 0; i < dir.size(); ++i) {
        partial.push_back(dir[i]);
        if (dir[i] == '/' || i + 1u == dir.size()) {
            if (!partial.empty()) {
                ::mkdir(partial.c_str(), mode);  // ignore EEXIST and any other failure here;
            }                                    // the subsequent file open() surfaces real errors.
        }
    }
}

// --- Monitor mode / channel control --------------------------------------------------

class CliMonitorControl final : public sec::IMonitorControl {
public:
    // Slow (airmon-ng commonly takes seconds): brings an interface into monitor mode and
    // resolves the resulting interface name via `iw dev`. Touches no object state, so it
    // is safe to run on a worker thread; the caller commits the result via applyUp().
    //
    // NOTE: `airmon-ng check kill` stops NetworkManager/wpa_supplicant and will drop your
    // managed WiFi connection — standard for monitor mode, but expect to go offline.
    static std::string bringUpMonitor(const std::string& iface) {
        runCapture({"airmon-ng", "check", "kill"});
        runCapture({"airmon-ng", "start", iface});
        return findMonitorInterface(iface);  // "" if no monitor interface resulted
    }

    // Slow: tears down monitor mode on `monitorIface`. Touches no object state; the
    // caller commits the result via applyDown().
    static void takeDownMonitor(const std::string& monitorIface) {
        runCapture({"airmon-ng", "stop", monitorIface});
    }

    // Fast, UI-thread-only state commit from a bringUpMonitor()/takeDownMonitor() result.
    void applyUp(const std::string& mon) {
        if (mon.empty()) {
            isMonitor_ = false;
            monitorIface_.clear();
        } else {
            monitorIface_ = mon;
            isMonitor_ = true;
        }
    }
    void applyDown() {
        isMonitor_ = false;
        monitorIface_.clear();
    }

    // Synchronous convenience wrappers (bringUpMonitor/takeDownMonitor + the matching
    // apply*()). Fine for one-shot, non-interactive CLI use; an interactive front-end
    // driving a responsive UI loop should call the static halves directly from a worker
    // thread instead (see AppController::startMonitor/stopMonitor).
    std::string enableMonitor(const std::string& iface) override {
        const std::string mon = bringUpMonitor(iface);
        applyUp(mon);
        return mon;
    }

    void disableMonitor() override {
        if (!isMonitor_) {
            return;
        }
        takeDownMonitor(monitorIface_);
        applyDown();
    }

    bool setChannel(int channel) override {
        if (!isMonitor_ || channel <= 0) {
            return false;
        }
        const CommandResult r =
            runCapture({"iw", "dev", monitorIface_, "set", "channel", std::to_string(channel)});
        return r.exitCode == 0;
    }

    bool isMonitor() const override { return isMonitor_; }
    std::string monitorIface() const override { return monitorIface_; }

private:
    // Parse `iw dev` for an interface currently in "type monitor". Prefers one whose name
    // relates to `base` (e.g. wlan0mon / wlan0), else returns the first monitor interface.
    static std::string findMonitorInterface(const std::string& base) {
        const CommandResult dev = runCapture({"iw", "dev"});
        std::istringstream in(dev.stdoutText);
        std::string line;
        std::string current;
        std::string firstMonitor;
        std::string relatedMonitor;
        while (std::getline(in, line)) {
            const std::string t = trimLine(line);
            if (t.rfind("Interface ", 0) == 0) {
                current = t.substr(10);
            } else if (t.find("type monitor") != std::string::npos && !current.empty()) {
                if (firstMonitor.empty()) {
                    firstMonitor = current;
                }
                if (current.find(base) != std::string::npos) {
                    relatedMonitor = current;
                }
            }
        }
        return !relatedMonitor.empty() ? relatedMonitor : firstMonitor;
    }

    static std::string trimLine(const std::string& s) {
        std::size_t a = 0;
        std::size_t b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t')) {
            ++a;
        }
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) {
            --b;
        }
        return s.substr(a, b - a);
    }

    bool isMonitor_ = false;
    std::string monitorIface_;
};

// --- airodump-ng CSV capture source --------------------------------------------------
//
// Enumerates APs and associated stations (channel-hopping handled by airodump itself).
// Frame-level events (deauth/EAPOL/PMKID) require the native libpcap source; see
// native_backend.hpp. poll() emits deltas so the engine sees each AP/station once.

class CliCaptureSource final : public sec::ICaptureSource {
public:
    ~CliCaptureSource() override { stop(); }

    bool start(const std::string& monitorIface) override {
        if (running_) {
            return true;
        }
        // Create an isolated private temporary directory (mode 0700) to prevent symlink/TOCTOU
        // attacks and protect sniffed wireless telemetry on multi-user systems.
        char templ[] = "/tmp/zd-airodump-XXXXXX";
        char* d = ::mkdtemp(templ);
        if (d == nullptr) {
            return false;
        }
        tempDir_ = d;
        prefix_ = tempDir_ + "/capture";
        csvPath_ = prefix_ + "-01.csv";
        const bool ok = proc_.start({"airodump-ng", "--output-format", "csv", "--write-interval",
                                     "1", "-w", prefix_, monitorIface});
        running_ = ok;
        if (!ok) {
            cleanTempDir();
        }
        return ok;
    }

    std::vector<sec::CapturedFrame> poll() override {
        std::vector<sec::CapturedFrame> frames;
        if (!running_) {
            return frames;
        }
        const std::string csv = readFileToString(csvPath_);
        if (csv.empty()) {
            return frames;
        }
        const AirodumpDump dump = parseAirodumpCsv(csv);
        const uint32_t now = linux_app::nowMs();
        for (const AirodumpAp& ap : dump.aps) {
            sec::CapturedFrame f;
            f.nowMs = now;
            f.kind = sec::FrameKind::Beacon;
            f.bssid = ap.bssid;
            f.ssid = ap.ssid;
            f.channel = ap.channel;
            f.rssi = ap.rssi;
            f.enc = ap.enc;
            frames.push_back(f);  // reduce() de-dupes by BSSID; re-emission just refreshes
        }
        for (const AirodumpStation& st : dump.stations) {
            if (st.bssid.empty() || sec::isBroadcastMac(st.bssid)) {
                continue;
            }
            const std::string key = st.mac + "@" + st.bssid;
            if (seenStations_.count(key) != 0u) {
                continue;
            }
            seenStations_.insert(key);
            sec::CapturedFrame f;
            f.nowMs = now;
            f.kind = sec::FrameKind::Data;
            f.bssid = st.bssid;
            f.source = st.mac;
            frames.push_back(f);
        }
        return frames;
    }

    void stop() override {
        proc_.stop();
        running_ = false;
        cleanTempDir();
    }

    bool running() const override { return running_ && proc_.running(); }

private:
    void cleanTempDir() {
        if (tempDir_.empty()) {
            return;
        }
        if (!csvPath_.empty()) {
            std::remove(csvPath_.c_str());
            csvPath_.clear();
        }
        std::remove((prefix_ + "-01.kismet.csv").c_str());
        std::remove((prefix_ + "-01.kismet.netxml").c_str());
        std::remove((prefix_ + "-01.log.csv").c_str());
        ::rmdir(tempDir_.c_str());
        tempDir_.clear();
        prefix_.clear();
    }

    DetachedProcess proc_;
    std::string tempDir_;
    std::string prefix_;
    std::string csvPath_;
    std::set<std::string> seenStations_;
    bool running_ = false;
};

// --- Active scan (no root needed; NetworkManager) ------------------------------------
//
// Both front-ends drive this from one periodic UI-thread tick, so the rescan kick +
// settle wait + list fetch (each a blocking nmcli subprocess call) run entirely on a
// worker thread via AsyncJob; pollScan() only ever inspects whether that job has finished.

class CliRadioScanner final : public zd::hal::IRadioScanner {
public:
    explicit CliRadioScanner(std::string iface = "wlan0") : iface_(std::move(iface)) {}

    // Detailed results are retained so the shell can feed BSSID/encryption into secops.
    // Only ever written on the UI thread (inside pollScan(), after the worker has finished).
    const std::vector<ScannedAp>& detailed() const { return detailed_; }

    bool startAsyncScan() override {
        return job_.start([]() {
            // Runs entirely on the worker thread: kick a rescan, give NetworkManager a
            // moment to populate fresh results, then fetch them as plain text. Touches
            // no scanner state - the UI thread parses the text once collect() returns it.
            runCapture({"nmcli", "dev", "wifi", "rescan"});
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            const CommandResult r = runCapture(
                {"nmcli", "-t", "-f", "BSSID,SSID,CHAN,SECURITY,SIGNAL", "dev", "wifi", "list"});
            return r.stdoutText;
        });
    }

    zd::hal::ScanPollResult pollScan() override {
        if (job_.running()) {
            return {zd::hal::ScanPollState::Running, {}};
        }
        const std::optional<std::string> text = job_.collect();
        if (!text.has_value()) {
            return {zd::hal::ScanPollState::Running, {}};
        }
        detailed_ = parseNmcliScanDetailed(*text);
        std::vector<AccessPoint> aps = parseNmcliScan(*text);
        return {zd::hal::ScanPollState::Completed, std::move(aps)};
    }

    void clearScan() override {}

private:
    std::string iface_;
    std::vector<ScannedAp> detailed_;
    linux_app::AsyncJob<std::string> job_;
};

// --- Deauth injection (gate enforced by SecSession before this is reached) ------------

class CliInjector final : public sec::IInjector {
public:
    explicit CliInjector(sec::IMonitorControl* monitor) : monitor_(monitor) {}

    bool available() const override {
        return monitor_ != nullptr && monitor_->isMonitor();
    }

    sec::InjectResult deauth(const std::string& bssid, const std::string& client,
                             uint32_t count) override {
        sec::InjectResult res;
        res.decision = sec::AuthDecision::Allowed;  // caller already cleared the gate
        if (monitor_ == nullptr || !monitor_->isMonitor()) {
            res.transmitted = false;
            res.detail = "no monitor interface";
            return res;
        }
        std::vector<std::string> argv = {"aireplay-ng", "--deauth", std::to_string(count), "-a",
                                         bssid};
        if (!client.empty() && !sec::isBroadcastMac(client)) {
            argv.push_back("-c");
            argv.push_back(client);
        }
        argv.push_back(monitor_->monitorIface());
        const CommandResult r = runCapture(argv);
        res.transmitted = (r.exitCode == 0);
        res.detail = res.transmitted ? ("aireplay-ng sent " + std::to_string(count) + " deauth")
                                     : ("aireplay-ng failed (exit " + std::to_string(r.exitCode) +
                                        ")");
        return res;
    }

private:
    sec::IMonitorControl* monitor_;
};

// --- Battery via sysfs ---------------------------------------------------------------

class SysfsPowerSensor final : public zd::hal::IPowerSensor {
public:
    zd::hal::PowerStatus readPower() override {
        zd::hal::PowerStatus st{100, false};
        const char* bases[] = {"/sys/class/power_supply/BAT0", "/sys/class/power_supply/BAT1"};
        for (const char* base : bases) {
            const std::string cap = readFileToString(std::string(base) + "/capacity");
            if (!cap.empty()) {
                st.batteryPercent = std::atoi(cap.c_str());
                const std::string status = readFileToString(std::string(base) + "/status");
                st.isCharging = (status.find("Charging") != std::string::npos) ||
                                (status.find("Full") != std::string::npos);
                return st;
            }
        }
        return st;  // desktop with no battery => 100%, not charging
    }
};

// --- File-backed theme store ---------------------------------------------------------

class FileThemeStore final : public zd::hal::IThemeStore {
public:
    FileThemeStore() {
        const std::string dir = homeDir() + "/.config/pwnagotchi-zeroday";
        makeDirRecursive(dir);
        path_ = dir + "/theme";
    }

    std::string loadThemeKey() override {
        const std::string raw = readFileToString(path_);
        std::string key;
        for (char c : raw) {
            if (c == '\n' || c == '\r' || c == ' ') {
                break;
            }
            key.push_back(c);
        }
        return key;
    }

    void saveThemeKey(std::string_view key) override {
        // Enforce 0600 permissions (read/write only by owner)
        const int fd = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) {
            ::fchmod(fd, 0600);
            const std::string entry = std::string(key.data(), key.size()) + "\n";
            const ssize_t written = ::write(fd, entry.data(), entry.size());
            (void)written;
            ::close(fd);
        }
    }

private:
    std::string path_;
};

// --- Append-only audit log for every transmit decision -------------------------------

class FileAuditLog final : public sec::IAuditLog {
public:
    FileAuditLog() {
        const std::string dir = homeDir() + "/.local/share/pwnagotchi-zeroday";
        makeDirRecursive(dir);
        path_ = dir + "/audit.log";
    }

    void record(const std::string& line) override {
        // Enforce 0600 permissions so audit trail is private and protected from tampering
        const int fd = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (fd < 0) {
            return;
        }
        ::fchmod(fd, 0600);
        const std::time_t t = std::time(nullptr);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
        const std::string entry = std::string(ts) + " " + line + "\n";
        const ssize_t written = ::write(fd, entry.data(), entry.size());
        (void)written;
        ::close(fd);
    }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

}  // namespace linux_backend
}  // namespace zd
