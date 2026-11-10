// pwnagotchi-zeroday — Linux desktop CLI / TUI shell.
//
// Two front-ends over the shared AppController:
//   * an interactive ncurses dashboard (pet face + live network table + defensive alerts),
//   * scriptable subcommands for headless use / automation.
//
// OFFENSIVE SAFETY: transmit (deauth) is refused unless BOTH --i-am-authorized is given
// AND the target BSSID is explicitly allow-listed (via --allow or the TUI). Every decision
// is written to the audit log. Only use the active features on networks you own or are
// explicitly authorized to test.
//
// Usage:
//   zd-linux [--iface wlan0] tui                 Interactive dashboard (default on a TTY)
//   zd-linux scan                                One active scan, print table, exit
//   zd-linux watch [--channel N]                 Monitor mode: stream defensive alerts
//   zd-linux deauth <BSSID> [--client <MAC>] [--count N] --i-am-authorized --allow <BSSID>
//   zd-linux --help

#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <ncurses.h>

#include "zd/hal.hpp"
#include "zd/theme.hpp"
#include "zd/ui_model.hpp"
#include "../app/controller.hpp"

namespace {

using zd::linux_app::AppController;
using zd::linux_app::nowMs;

std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop.store(true); }

struct Args {
    std::string iface = "wlan0";
    std::string command;  // tui | scan | watch | deauth | help
    std::string bssid;
    std::string client;
    uint32_t count = 16;
    int channel = 0;
    bool authorized = false;
    std::vector<std::string> allow;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--iface" && i + 1 < argc) {
            a.iface = argv[++i];
        } else if (s == "--client" && i + 1 < argc) {
            a.client = argv[++i];
        } else if (s == "--count" && i + 1 < argc) {
            a.count = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (s == "--channel" && i + 1 < argc) {
            a.channel = std::atoi(argv[++i]);
        } else if (s == "--i-am-authorized") {
            a.authorized = true;
        } else if (s == "--allow" && i + 1 < argc) {
            a.allow.push_back(argv[++i]);
        } else if (s == "--help" || s == "-h") {
            a.command = "help";
        } else if (a.command.empty()) {
            a.command = s;
        } else if (a.bssid.empty()) {
            a.bssid = s;  // positional target for deauth
        }
    }
    return a;
}

const char* encLabel(zd::sec::Encryption e) { return zd::sec::encryptionLabel(e); }

// AppController's monitor-start/stop and deauth are always async now (they shell out to
// airmon-ng/aireplay-ng, which can take a while, and must never block an interactive UI
// loop). These one-shot, non-interactive subcommands aren't driving a UI loop, so it's
// fine for them to spin here until the in-flight action finishes.
zd::linux_app::ActionResult waitForAsyncResult(AppController& app) {
    for (int i = 0; i < 150 && app.asyncBusy(); ++i) {
        app.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (auto r = app.takeLastAsyncResult()) {
        return *r;
    }
    return {false, "action timed out"};
}

void printHelp() {
    std::cout <<
        "pwnagotchi-zeroday Linux CLI/TUI\n"
        "  zd-linux [--iface wlan0] tui            Interactive dashboard (default on a TTY)\n"
        "  zd-linux scan                           One active scan, print table\n"
        "  zd-linux watch [--channel N]            Monitor mode; stream defensive alerts\n"
        "  zd-linux deauth <BSSID> [--client MAC] [--count N] --i-am-authorized --allow <BSSID>\n"
        "\nSafety: deauth requires --i-am-authorized AND --allow <that BSSID>. All transmit\n"
        "decisions are logged. Only test networks you own or are authorized to assess.\n";
}

// --- Headless subcommands ------------------------------------------------------------

int runScan(AppController& app) {
    app.requestScan();
    // Drive the controller until the scan completes (bounded).
    for (int i = 0; i < 100 && !app.petState().hasScannedOnce; ++i) {
        app.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // One extra settle so results propagate into the security engine.
    for (int i = 0; i < 5; ++i) {
        app.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
    const auto targets = zd::sec::targetsByStrength(app.secState());
    std::printf("%-18s %-24s %4s %6s %-6s\n", "BSSID", "SSID", "CH", "dBm", "ENC");
    for (const auto& t : targets) {
        std::printf("%-18s %-24s %4d %6d %-6s\n", t.bssid.c_str(),
                    (t.ssid.empty() ? "<hidden>" : t.ssid.substr(0, 24).c_str()), t.channel,
                    t.rssi, encLabel(t.enc));
    }
    std::printf("\n%zu networks seen.\n", targets.size());
    return 0;
}

int runWatch(AppController& app, const Args& a) {
    const auto kick = app.startMonitor();
    std::fprintf(stderr, "[monitor] %s\n", kick.message.c_str());
    if (!kick.ok) {
        return 1;
    }
    const auto res = waitForAsyncResult(app);
    std::fprintf(stderr, "[monitor] %s\n", res.message.c_str());
    if (!res.ok) {
        return 1;
    }
    if (a.channel > 0) {
        app.setChannel(a.channel);
    }
    std::fprintf(stderr, "[watch] streaming defensive alerts (Ctrl-C to stop)...\n");
    std::size_t printed = 0;
    while (!g_stop.load()) {
        app.step();
        const auto& alerts = app.secState().alerts;
        for (std::size_t i = printed; i < alerts.size(); ++i) {
            const auto& al = alerts[i];
            std::printf("[%8u ms] %-14s %-17s %s\n", al.nowMs, zd::sec::alertKindLabel(al.kind),
                        al.bssid.c_str(), al.message.c_str());
            std::fflush(stdout);
        }
        printed = alerts.size();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }
    app.stopMonitor();
    waitForAsyncResult(app);  // let airmon-ng stop finish before the process exits
    return 0;
}

int runDeauth(AppController& app, const Args& a) {
    if (a.bssid.empty()) {
        std::fprintf(stderr, "deauth: missing target BSSID\n");
        return 2;
    }
    if (!zd::sec::isValidMac(a.bssid)) {
        std::fprintf(stderr, "deauth: invalid target BSSID '%s'\n", a.bssid.c_str());
        return 2;
    }
    if (!a.client.empty() && !zd::sec::isValidMac(a.client)) {
        std::fprintf(stderr, "deauth: invalid --client MAC '%s'\n", a.client.c_str());
        return 2;
    }
    for (const std::string& b : a.allow) {
        if (!zd::sec::isValidMac(b)) {
            std::fprintf(stderr, "deauth: invalid --allow BSSID '%s'\n", b.c_str());
            return 2;
        }
    }
    app.confirmAuthorization(a.authorized);
    for (const std::string& b : a.allow) {
        app.allowBssid(b);
    }
    const auto kickMon = app.startMonitor();
    std::fprintf(stderr, "[monitor] %s\n", kickMon.message.c_str());
    if (!kickMon.ok) {
        return 1;
    }
    const auto mon = waitForAsyncResult(app);
    std::fprintf(stderr, "[monitor] %s\n", mon.message.c_str());
    if (!mon.ok) {
        return 1;
    }
    if (a.channel > 0) {
        app.setChannel(a.channel);
    }
    const auto kickDeauth = app.requestDeauth(a.bssid, a.client, a.count);
    std::fprintf(stderr, "[deauth] %s\n", kickDeauth.message.c_str());
    if (!kickDeauth.ok) {
        app.stopMonitor();
        waitForAsyncResult(app);
        return 1;
    }
    const auto r = waitForAsyncResult(app);
    std::fprintf(stderr, "[deauth] %s\n", r.message.c_str());
    std::fprintf(stderr, "[audit ] see %s\n", app.auditPath().c_str());
    app.stopMonitor();
    waitForAsyncResult(app);
    return r.ok ? 0 : 1;
}

// --- ncurses dashboard ---------------------------------------------------------------

void drawDashboard(AppController& app, int selected, const std::string& lastActionMsg,
                   bool busy) {
    erase();
    int maxy = 0;
    int maxx = 0;
    getmaxyx(stdscr, maxy, maxx);

    // Header: pet face + status.
    const zd::UiFrame frame = app.petFrame(zd::ScreenDimensions{240, 135});
    const std::string ascii = zd::renderAsciiPreview(frame);
    const zd::AppState& ps = app.petState();
    attron(A_BOLD);
    mvprintw(0, 1, "pwnagotchi-zeroday  [%s]  mood:%s  batt:%d%%  engine:%s",
             zd::themeSpec(ps.theme).name, zd::faceForMood(ps.mood).moodLabel, ps.batteryPercent,
             app.monitoring() ? app.engineLabel().c_str() : "idle (scan only)");
    attroff(A_BOLD);
    mvprintw(1, 1, "\"%s\"", ps.phrase.c_str());

    // Authorization banner.
    const auto& auth = app.authorization();
    const bool armed = auth.operatorConfirmed;
    attron(armed ? COLOR_PAIR(2) : COLOR_PAIR(1));
    mvprintw(2, 1, " AUTH: %s  allow-list:%zu%s ", armed ? "CONFIRMED" : "not confirmed",
             auth.allowedBssids.size(), auth.allowAll ? " (ALL!)" : "");
    attroff(armed ? COLOR_PAIR(2) : COLOR_PAIR(1));

    // Channel distribution across both bands: the Linux backend sees real 5GHz channel
    // numbers (unlike the portable 2.4GHz-only Channel Spectrum pet-UI page).
    {
        const auto dist = zd::sec::channelDistribution(app.secState());
        std::string band24;
        std::string band5;
        for (const auto& c : dist) {
            const std::string part =
                "ch" + std::to_string(c.channel) + "x" + std::to_string(c.count) + " ";
            if (c.band == zd::sec::Band::Band24GHz) {
                band24 += part;
            } else if (c.band == zd::sec::Band::Band5GHz) {
                band5 += part;
            }
        }
        mvprintw(3, 1, "2.4GHz: %-28s 5GHz: %s", band24.empty() ? "-" : band24.c_str(),
                 band5.empty() ? "-" : band5.c_str());
        clrtoeol();
    }

    // Network table.
    const auto targets = zd::sec::targetsByStrength(app.secState());
    int row = 4;
    attron(A_UNDERLINE);
    mvprintw(row++, 1, "%-18s %-20s %3s %5s %-5s %4s %-4s", "BSSID", "SSID", "CH", "dBm", "ENC",
             "CLI", "HS");
    attroff(A_UNDERLINE);
    const int tableRows = (maxy - row - 8 > 0) ? (maxy - row - 8) : 4;
    for (int i = 0; i < static_cast<int>(targets.size()) && i < tableRows; ++i) {
        const auto& t = targets[i];
        const bool sel = (i == selected);
        if (sel) {
            attron(A_REVERSE);
        }
        mvprintw(row + i, 1, "%-18s %-20s %3d %5d %-5s %4u %-4s", t.bssid.c_str(),
                 (t.ssid.empty() ? "<hidden>" : t.ssid.substr(0, 20).c_str()), t.channel, t.rssi,
                 encLabel(t.enc), t.clientCount,
                 t.handshake ? "HS" : (t.pmkid ? "PMK" : "-"));
        if (sel) {
            attroff(A_REVERSE);
        }
    }

    // Alerts (defensive) pane.
    const auto& alerts = app.secState().alerts;
    int aRow = maxy - 7;
    attron(A_BOLD);
    mvprintw(aRow++, 1, "Defensive alerts:");
    attroff(A_BOLD);
    const int shown = 4;
    const int start = (static_cast<int>(alerts.size()) > shown)
                          ? static_cast<int>(alerts.size()) - shown
                          : 0;
    for (int i = start; i < static_cast<int>(alerts.size()); ++i) {
        const auto& al = alerts[i];
        const int cp = (al.severity == zd::sec::Severity::Critical) ? 1 : 3;
        attron(COLOR_PAIR(cp));
        mvprintw(aRow++, 2, "%-14s %-17s %s", zd::sec::alertKindLabel(al.kind), al.bssid.c_str(),
                 al.message.substr(0, static_cast<std::size_t>(maxx - 36)).c_str());
        attroff(COLOR_PAIR(cp));
    }

    // Last background action result (monitor start/stop, deauth): these run on a worker
    // thread and never block this loop, so their outcome surfaces here once it arrives.
    if (busy || !lastActionMsg.empty()) {
        attron(A_BOLD);
        mvprintw(maxy - 2, 1, "%s", busy ? "working..." : lastActionMsg.c_str());
        clrtoeol();
        attroff(A_BOLD);
    }

    // Key help.
    mvprintw(maxy - 1, 1,
             "[s]can [m]onitor [t]heme [v]iew up/dn:sel [A]uth [L]allow sel [x]deauth sel [q]uit");
    refresh();
}

std::string promptLine(const char* label) {
    echo();
    curs_set(1);
    int maxy = 0;
    int maxx = 0;
    getmaxyx(stdscr, maxy, maxx);
    (void)maxx;
    mvprintw(maxy - 2, 1, "%s", label);
    clrtoeol();
    char buf[128] = {0};
    getnstr(buf, sizeof(buf) - 1);
    noecho();
    curs_set(0);
    return std::string(buf);
}

int runTui(AppController& app) {
    if (!isatty(STDOUT_FILENO)) {
        std::fprintf(stderr, "tui: stdout is not a terminal; use 'scan' or 'watch'.\n");
        return 2;
    }
    initscr();
    noecho();
    cbreak();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_RED, -1);     // critical / blocked
        init_pair(2, COLOR_GREEN, -1);   // armed / ok
        init_pair(3, COLOR_YELLOW, -1);  // warning
    }

    app.requestScan();
    int selected = 0;
    uint32_t lastStep = 0;
    std::string lastActionMsg;
    while (!g_stop.load()) {
        const uint32_t now = nowMs();
        if (now - lastStep >= 100u) {
            app.step();
            lastStep = now;
            if (auto r = app.takeLastAsyncResult()) {
                lastActionMsg = r->message;
            }
        }
        drawDashboard(app, selected, lastActionMsg, app.asyncBusy());

        const int ch = getch();
        const auto targets = zd::sec::targetsByStrength(app.secState());
        if (ch == 'q') {
            break;
        } else if (ch == 's') {
            app.requestScan();
        } else if (ch == 't') {
            app.dispatchPet(zd::CycleThemeEvent{nowMs(), +1});
        } else if (ch == 'v') {
            app.dispatchPet(zd::CyclePageEvent{nowMs(), +1});
        } else if (ch == KEY_DOWN) {
            if (!targets.empty()) {
                selected = (selected + 1) % static_cast<int>(targets.size());
            }
        } else if (ch == KEY_UP) {
            if (!targets.empty()) {
                selected = (selected + static_cast<int>(targets.size()) - 1) %
                           static_cast<int>(targets.size());
            }
        } else if (ch == 'm') {
            const auto r = app.monitoring() ? app.stopMonitor() : app.startMonitor();
            lastActionMsg = r.message;  // immediate ack; the real outcome follows async
        } else if (ch == 'A') {
            app.confirmAuthorization(!app.authorization().operatorConfirmed);
        } else if (ch == 'L') {
            if (selected < static_cast<int>(targets.size())) {
                app.allowBssid(targets[selected].bssid);
            }
        } else if (ch == 'x') {
            if (selected < static_cast<int>(targets.size())) {
                const std::string bssid = targets[selected].bssid;
                const std::string cnt = promptLine(("deauth count for " + bssid + " (Enter=16): ").c_str());
                const uint32_t count = cnt.empty() ? 16u : static_cast<uint32_t>(std::atoi(cnt.c_str()));
                const auto r = app.requestDeauth(bssid, "", count);
                lastActionMsg = "deauth: " + r.message;  // immediate ack; real result follows async
            }
        } else if (ch == 'c') {
            const std::string c = promptLine("set channel: ");
            if (!c.empty()) {
                app.setChannel(std::atoi(c.c_str()));
            }
        }
        napms(30);
    }
    if (app.monitoring()) {
        app.stopMonitor();
        waitForAsyncResult(app);
    }
    endwin();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    const Args a = parseArgs(argc, argv);

    if (a.command == "help") {
        printHelp();
        return 0;
    }

    AppController app(a.iface);
    app.boot();

    std::string cmd = a.command;
    if (cmd.empty()) {
        cmd = isatty(STDOUT_FILENO) ? "tui" : "scan";
    }

    if (cmd == "tui") {
        return runTui(app);
    }
    if (cmd == "scan") {
        return runScan(app);
    }
    if (cmd == "watch") {
        return runWatch(app, a);
    }
    if (cmd == "deauth") {
        return runDeauth(app, a);
    }
    std::fprintf(stderr, "unknown command '%s'\n", cmd.c_str());
    printHelp();
    return 2;
}
