# Cross-Platform Architecture & Device Porting Guide

pwnagotchi-zeroday separates **portable pure C++17 domain/UI logic**, **portable immediate-mode pixel rendering**, and **thin device-specific shells** so adding a new microcontroller, handheld cyberdeck, SBC + e-Paper HAT, or desktop app requires writing only ~150–220 lines of glue code.

---

## Layered Directory Structure

```text
pwnagotchi-zeroday/
├── core/include/zd/              # Layer 1: Zero-dependency C++17 Functional Core + HAL
│   ├── fp.hpp                    # Pure higher-order combinators & stateless xorshift32 PRNG
│   ├── domain.hpp                # Immutable AppState, ThemeId, and semantic AppEvents
│   ├── theme.hpp                 # 10-theme RGB888 catalog + RGB565 / RGB888 / Luma helpers
│   ├── wifi_analysis.hpp         # Pure SSID sanitization, mesh deduplication & histograms
│   ├── pet_logic.hpp             # Pure reducer: reduce(AppState, AppEvent) -> AppState
│   ├── ui_model.hpp              # Multi-device responsive layout (128x64 up to 320x240+)
│   ├── hal.hpp                   # Portable HAL contracts, CLI parser & PlatformSession
│   ├── secops.hpp                # Pure security-operations reducer, auth gate & wire replay
│   └── secops_hal.hpp            # Security backend contracts (monitor, capture, TX, audit)
├── render/include/zd/render/     # Layer 2: Hardware-Agnostic Pixel/Vector Renderer
│   ├── painter.hpp               # Templated paintUiFrame<Gfx> for M5GFX, LovyanGFX, etc.
│   └── surface_rgb565.hpp        # Zero-dep software RGB565 surface + 5x7 font + 1-bit export
├── platforms/                    # Layer 3: Imperative Hardware & OS Shells
│   ├── m5stick/main.cpp          # ESP32 + M5Unified + WiFi + NVS Preferences + Serial CLI
│   ├── tdeck/main.cpp            # ESP32-S3 + LovyanGFX + Trackball + Touch + Keyboard + GPS
│   ├── linux/                    # Desktop Linux CLI/ncurses TUI (`zd-linux`) + Qt6 GUI (`zd-gui`)
│   ├── native/main.cpp           # Linux / macOS / Windows / RPi terminal simulator & PPM exporter
│   └── web/                      # Standalone HTML5 Canvas + JS functional core port
└── tests/                        # Automated Host & Cross-Language Parity Tests
    ├── core/test_functional_core.cpp
    ├── core/test_secops.cpp
    ├── core/test_linux_parsers.cpp
    └── render/test_painter.cpp
```

---

## Built-In Responsive Display Tiers

`zd::buildUiFrame(state, {width, height})` adapts automatically to **any** `ScreenDimensions` (`>= 96x56`):

| Profile Key | Example Resolution | Typical Hardware Targets |
| :--- | :---: | :--- |
| `micro` | `128×64` | SSD1306 / SH1106 I2C/SPI OLEDs, RP2040 / STM32 / RPi OLED bonnets |
| `compact` | `160×80` | Original M5StickC, ST7735 0.96" IPS modules |
| `plus` | `240×135` | M5StickC Plus / Plus2, M5Cardputer, LilyGO T-Display |
| `epaper` | `250×122` | Waveshare 2.13" e-Paper HAT (Raspberry Pi Zero / Linux SBCs) |
| `wide` | `320×240`+ | M5Stack Core / Core2 / CoreS3, ESP32 CYD, LilyGO T-Deck |

---

## Porting to a New Device in 4 Small Adapters

Include [`zd/hal.hpp`](../core/include/zd/hal.hpp) and create a `platforms/<your_device>/main.cpp` implementing up to four tiny `zd::hal` adapters:

1. **`zd::hal::IRadioScanner`** — `startAsyncScan()`, `pollScan()`, `clearScan()`
   - *ESP32 / Arduino*: `WiFi.scanNetworks(true, true)` (see [`platforms/m5stick/main.cpp`](../platforms/m5stick/main.cpp))
   - *Linux / Raspberry Pi*: non-blocking `nl80211` or async `iw dev wlan0 scan` parser
   - *RP2040 Pico W*: `cyw43_wifi_scan` callback pushing `zd::AccessPoint` entries into a buffer
2. **`zd::hal::IDisplaySink`** — `dimensions()`, `present(const zd::UiFrame&)`
   - *M5GFX / LovyanGFX / TFT_eSPI*: call `zd::render::paintUiFrame(sprite, frame)` directly!
   - *Raw SPI TFT / Linux `/dev/fb0` / SDL*: render into `zd::render::Rgb565Surface surf(w, h); zd::render::paintUiFrame(surf, frame);` and blit `surf.pixels()` or `surf.toRgb888Buffer()`.
   - *1-Bit Monochrome OLED / Waveshare e-Paper*: render into `zd::render::Rgb565Surface`, then call `surf.toMono1BitPacked(frame.palette.bg)` for a ready-to-clock MSB-first 1-bit bitmap!
3. **`zd::hal::IPowerSensor`** *(optional)* — return `{batteryPercent, isCharging}`, or use `zd::hal::FixedPowerSensor` on USB-powered boards.
4. **`zd::hal::IThemeStore`** *(optional)* — persist `std::string_view key` (`"neon"`, `"operator"`, ...) to NVS, LittleFS, EEPROM, or `~/.config/`, or use `zd::hal::MemoryThemeStore`. `zd::hal::PlatformSession` only writes when the theme actually changes to avoid flash wear.

### Mapping Diverse Input Controls

Devices are not limited to 2 buttons — map physical buttons, D-pads, rotary encoders, touchscreens, or keyboards onto semantic events in [`core/include/zd/domain.hpp`](../core/include/zd/domain.hpp):

- `zd::CyclePageEvent{nowMs, +1}` / `zd::CyclePageEvent{nowMs, -1}` / `zd::SetPageEvent{nowMs, page}`
- `zd::CycleThemeEvent{nowMs, +1}` / `zd::CycleThemeEvent{nowMs, -1}` / `zd::SetThemeEvent{nowMs, theme}`
- `zd::ButtonBPressedEvent{nowMs}` (request immediate scan)

Finally, add a PlatformIO `[env:<your-board>]` section in [`platformio.ini`](../platformio.ini) with `build_src_filter = -<*> +<<your_device>/*>` (or a CMake executable target in [`CMakeLists.txt`](../CMakeLists.txt)).
