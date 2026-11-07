# Pwnagotchi-ZeroDay
Pwnagotchi, our way zero day

<p align="center">
  <img src="assets/web_toy_preview.png" alt="Pwnagotchi-ZeroDay Web Port & Pocket Cyberdeck" width="620" />
</p>

A cross-platform Pwnagotchi-inspired virtual pet UI and 2.4 GHz WiFi scanner written in portable **C++17**, built around a **Functional Core, Imperative Shell** architecture that runs across **ESP32 / M5Stack handhelds**, **native desktop / Linux SBC terminals**, and the **browser**.

---

## Features

- **10 Original, Trademark-Clean Themes**: Cycle forward and backward through `Paper Ink`, `Dark Ink`, `ZeroDay`, `Phosphor`, `Neon Noir` (magenta/cyan neon nightlife), `Neon Volt` (acid-yellow/cyan visor), `Operator` (cobalt-blue & steel security workstation), `Red Team` (night-ops red), `Amber CRT`, and `Sunset Grid` — complete with an on-screen toast overlay, pet reactions, and NVS/local persistence. See [docs/THEMES.md](docs/THEMES.md).
- **Expressive Pwnagotchi-Style Faces**: Crisp vector/pixel expressions (`Awake`, `LookingLeft`, `LookingRight`, `Happy`, `Excited`, `Cool`, `Lonely`, `Bored`, `Sleepy`) rendered tear-free via `zd::render::paintUiFrame`.
- **Non-Blocking WiFi Scanning**: Pure functional scan pipeline sanitizes SSIDs, deduplicates mesh BSSIDs, builds channel 1–13 histograms, and drives mood & commentary without blocking animations or inputs.
- **Three Switchable Views**:
  1. **Pet Face View**: Split layout with animated face, mood badge, word-wrapped `zeroday>` status phrase, strongest SSID spotlight, and telemetry bars.
  2. **Nearby WiFi Radar**: Top deduplicated access points sorted by RSSI with 4-bar signal meters, security indicator (`*`), channel, and dBm readout.
  3. **Channel Spectrum**: 2.4 GHz channels 1–13 bar histogram with open vs. secured AP counts.
- **Multi-Device Responsive Layout**: Automatically scales from `128×64` micro OLEDs and `160×80` M5StickC up through `240×135` StickC Plus2 / Cardputer, `250×122` 2.13" e-Paper HATs, and `320×240`+ QVGA cyberdecks (`M5Stack Core2`, `CYD`, `T-Deck`), including built-in RGB565, RGB888, and 1-bit monochrome framebuffer export. See [docs/PORTING.md](docs/PORTING.md).

---

## Cross-Platform Repository Layout

```text
core/include/zd/              # Zero-dependency C++17 Functional Core & HAL
  ├── fp.hpp                  # Higher-order combinators & stateless xorshift32 PRNG
  ├── domain.hpp              # Immutable AppState, ThemeId & semantic AppEvents
  ├── theme.hpp               # Single-source-of-truth 10-theme catalog & color helpers
  ├── wifi_analysis.hpp       # Pure scan sanitization, deduplication & histograms
  ├── pet_logic.hpp           # Pure Elm/Redux reducer: reduce(AppState, AppEvent)
  ├── ui_model.hpp            # Responsive multi-device layout & ASCII frame renderer
  └── hal.hpp                 # Portable HAL interfaces, CLI parser & PlatformSession
render/include/zd/render/     # Hardware-agnostic immediate-mode renderer
  ├── painter.hpp             # Templated paintUiFrame<Gfx> (M5GFX, LovyanGFX, etc.)
  └── surface_rgb565.hpp      # Zero-dep RGB565 framebuffer + 5x7 font + 1-bit export
platforms/                    # Device & OS shells
  ├── m5stick/main.cpp        # ESP32 + M5Unified + WiFi + NVS Preferences + Serial CLI
  ├── native/main.cpp         # Desktop / Linux / RPi CLI simulator & PPM exporter (zd_sim)
  └── web/                    # Standalone HTML5 Canvas pocket cyberdeck port
tests/                        # Automated host & cross-language parity test suites
docs/                         # Theme design rules (THEMES.md) & porting guide (PORTING.md)
```

---

## Controls

| Input | Action |
| :--- | :--- |
| **Button A (Front `M5`)** / `A` | Cycle screen views (`Pet Face` -> `WiFi Radar` -> `Channel Spectrum`) & wake pet |
| **Button B (Short press)** / `B` | Trigger an immediate WiFi scan |
| **Button B (Hold ~0.7s)** / `T` | Cycle to **next** theme (shows toast & persists to flash/storage) |
| **Power Click or Button C** / `Shift+T` | Cycle to **previous** theme |
| **Serial / Native / Web CLI** | `themes`, `theme [next\|prev\|<key>]`, `view [next\|prev]`, `scan`, `ascii`, `status` |

---

## Building & Running

### 1. Embedded Firmware (PlatformIO)

```bash
# M5StickC (160x80) / M5StickC Plus (240x135)
pio run -e m5stick-c --target upload

# M5StickC Plus2 (240x135, 8MB Flash)
pio run -e m5stick-c-plus2 --target upload

# M5Stack Core2 (320x240 QVGA)
pio run -e m5stack-core2 --target upload

# Serial monitor + interactive CLI (try typing: themes, theme neon, theme operator)
pio device monitor -b 115200
```

### 2. Native Desktop / Linux Simulator (`zd_sim`) & Unit Tests

Build and run all C++ core tests, software framebuffer/HAL tests, and Web/C++ parity tests with `make` (or `cmake`):

```bash
make test
```

Preview every theme in truecolor ANSI right in your terminal, or export a pixel-accurate `.ppm` screenshot for any device profile (`micro`, `compact`, `plus`, `epaper`, `wide`):

```bash
make demo
./build/zd_sim --theme neon --size plus
./build/zd_sim --theme operator --size wide --ppm build/operator_wide.ppm
```

With CMake + CTest:

```bash
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

### 3. Web Port (Browser Pocket Cyberdeck)

Open [`platforms/web/index.html`](platforms/web/index.html) directly in any browser (`file://` works with zero build step), or serve locally:

```bash
make web-serve
# Visit http://localhost:8080
```

Run the Web Port & C++ theme catalog parity tests directly with Node.js:

```bash
node platforms/web/test_web_core.js
```
