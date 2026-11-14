# pwnagotchi-zeroday — Linux Desktop (CLI/TUI + Qt6 GUI)

The Linux desktop target turns the portable pet/scanner core into a real wireless
reconnaissance and assessment tool, with **two front-ends** over one shared controller:

- **`zd-linux`** — an ncurses TUI dashboard plus scriptable subcommands (`scan`, `watch`, `deauth`).
- **`zd-gui`** — a Qt6 Widgets desktop app (pet screen, live network table, defensive alert feed, offensive panel).

Unlike the browser toy, this build touches real hardware and has **offensive and defensive**
capabilities. Read the [Authorization & safety](#authorization--safety) section before using
the active features.

---

## Capabilities

### Reconnaissance (unprivileged)
- Active scan via NetworkManager (`nmcli`) / `iw` — no root required.
- Per-AP intelligence: BSSID, SSID (incl. hidden), channel, signal, encryption (Open/WEP/WPA/WPA2/WPA3).

### Defensive monitoring (monitor mode)
Pure detectors in [`core/include/zd/secops.hpp`](../core/include/zd/secops.hpp), fed by live
802.11 frames:
- **Deauth/disassoc flood detection** — sliding-window burst detection per BSSID (jamming / handshake-forcing attacks against you).
- **Evil-twin detection** — one SSID advertised by multiple BSSIDs.
- **Open-downgrade detection** — a previously-encrypted SSID reappearing as Open (rogue/MITM bait).
- **Karma/mana beacon detection** — one BSSID advertising an abnormal number of SSIDs.

### Offensive (monitor mode, authorization-gated)
- Passive **WPA handshake** and **PMKID** capture from live EAPOL frames.
- **Deauthentication injection** to force handshakes — **only** against explicitly
  allow-listed BSSIDs, and only after you confirm authorization. Every decision is logged.

### Additional recon, threat & session capabilities (surfaced in TUI & GUI)
- **Station / probe-request tracking** — a `Station` record per client MAC (probed SSIDs,
  last-associated BSSID, probe count, and a `WEAK` risk flag via `hasWeakOrDefaultProbe()`
  when probing for common default/open-bait SSIDs). Viewable in the `zd-gui` **Stations / Probes**
  tab and by pressing `p` (or `Tab`) in the `zd-linux` TUI.
- **WPS detection (`WPS*`)** — decoded from the WPS vendor IE (`00:50:F2:04`) in monitor-mode
  beacons/probe-responses (`dot11.hpp`) and surfaced in a dedicated `WPS` column in both
  `zd-linux` and `zd-gui`.
- **`SecMood` ambient threat status** — a `CALM` / `WATCHFUL` / `ALARMED` signal purely
  derived from recent alert severity (`deriveSecMood`), displayed in the `zd-linux` TUI header,
  `zd-linux watch` alert stream, `zd-gui` status header, and as an amber/crimson bezel border on
  the `zd-gui` PetWidget.
- **Session record & deterministic replay** — pure, escaped text encoding for the `SecEvent` log
  (`encodeSecEvents`/`decodeSecEvents`/`replaySecEvents`), wired into `zd-linux` via
  `--record <file>`, `--replay <file>`, `zd-linux replay <file>`, and TUI `w`/`r` hotkeys, plus
  **Load Session…** / **Save Session…** buttons in `zd-gui`.

---

## Architecture (Functional Core, Imperative Shell)

```text
core/include/zd/
  secops.hpp         Pure security engine: SecState + reduce(), defensive monitors,
                     and the authorization gate authorize(auth, bssid). Zero OS headers.
  secops_hal.hpp     Adapter contracts (ICaptureSource / IInjector / IMonitorControl) and
                     SecSession — the transmit chokepoint that enforces the gate.
platforms/linux/
  backend/
    parsers.hpp      Pure parsers for nmcli / iw / airodump CSV (host-tested).
    dot11.hpp        Pure radiotap + 802.11 frame decoder (host-tested).
    process.hpp      fork/execvp subprocess helpers (argv vectors; no shell).
    cli_backend.hpp  CLI-tool adapters: airmon-ng/iw, airodump-ng, nmcli, aireplay-ng, sysfs.
    native_backend.hpp  libpcap frame capture + crafted-deauth injection (ZD_WITH_LIBPCAP).
  app/
    controller.hpp   AppController: wires pet + security sessions, backend, scheduling.
    clock.hpp        One monotonic ms clock shared by backend + controller.
  cli/main.cpp       ncurses TUI + scriptable subcommands.
  gui/               Qt6 Widgets dashboard (pet_widget, main_window).
```

The **hybrid backend**: native `libpcap` frame capture and injection are compiled in when
`ZD_WITH_LIBPCAP` is set (full frame-level deauth/EAPOL/PMKID visibility). The CLI tools
(`airodump-ng`, `aireplay-ng`) remain the fallback. Monitor-mode setup and active scan always
go through `iw`/`airmon-ng`/`nmcli`.

All policy — mood, layout, defensive detection, **and the transmit gate** — lives in the pure
cores and is unit-tested without hardware or root
([`tests/core/test_secops.cpp`](../tests/core/test_secops.cpp),
[`tests/core/test_linux_parsers.cpp`](../tests/core/test_linux_parsers.cpp)).

---

## Build

Dependencies (Debian/Kali):

```bash
sudo apt install cmake pkg-config qt6-base-dev libpcap-dev libncurses-dev \
                 aircrack-ng iw network-manager
```

Build both front-ends and run the tests:

```bash
cmake -B build_cmake && cmake --build build_cmake -j
ctest --test-dir build_cmake --output-on-failure
```

CLI only (no Qt/CMake needed, just g++ + ncurses + libpcap):

```bash
make linux        # builds build/zd-linux
make test         # runs the full host test suite
```

Binaries: `build_cmake/zd-linux`, `build_cmake/zd-gui`.

---

## Usage

### CLI / TUI

```bash
./zd-linux                       # interactive TUI dashboard (default on a terminal)
./zd-linux --iface wlan0 tui
./zd-linux scan                  # one active scan, print a table, exit (no root)
sudo ./zd-linux watch --record session.zds   # monitor mode: stream alerts & record session
./zd-linux replay session.zds    # deterministically replay a recorded session file
```

TUI keys: `s` scan · `m` toggle monitor · `p`/`Tab` toggle APs vs Stations/Probes ·
`t` theme · `v` view · ↑/↓ select row · `A` toggle authorization · `L` allow-list selected ·
`x` deauth selected · `c` set channel · `w` save session · `r` load session · `q` quit.

### GUI

```bash
./zd-gui --iface wlan0
./zd-gui --replay session.zds
```

Scan, toggle monitor mode, watch the network table and defensive alert feed fill in, and use
the offensive panel (disabled until you arm it — see below).

---

## Authorization & safety

Transmitting deauthentication frames is disruptive and, against networks you do not own or
have permission to test, is illegal in most jurisdictions. This tool enforces a hard gate in
pure code (`authorize()` in `secops.hpp`), consulted by `SecSession::requestDeauth()` before
any frame is sent:

**A deauth is sent only when BOTH are true:**
1. You have **confirmed authorization** (`--i-am-authorized`, or the GUI checkbox / TUI `A`).
2. The **target BSSID is on the allow-list** (`--allow <BSSID>`, GUI "Allow selected", or TUI `L`).

Otherwise the request is refused, counted, and written to the audit log with the reason.
There is no "attack everything" mode; the `allowAll` wildcard still requires explicit
confirmation and is intended only for isolated lab ranges you own.

```bash
# Example: force a handshake on a lab AP you own.
sudo ./zd-linux deauth AA:BB:CC:DD:EE:FF --count 16 \
     --i-am-authorized --allow AA:BB:CC:DD:EE:FF
```

Every transmit decision (allowed or blocked) is appended to
`~/.local/share/pwnagotchi-zeroday/audit.log`.

**Operational notes**
- Monitor mode and injection require **root** (`CAP_NET_RAW`/`CAP_NET_ADMIN`) and an adapter
  whose driver supports monitor mode + injection.
- Enabling monitor mode runs `airmon-ng check kill`, which **stops NetworkManager/wpa_supplicant
  and drops your managed WiFi connection** — expected, but you will go offline.
- Recon (`scan`) needs none of this and runs unprivileged.

Use this only for authorized security testing, your own networks, lab ranges, and education.
