# Pwnagotchi-ZeroDay
Pwnagotchi, our way zero day

A Pwnagotchi-inspired virtual pet UI and 2.4 GHz WiFi scanner written in **C++17** for the **M5Stick** family (**M5StickC**, **M5StickC Plus**, and **M5StickC Plus2**), built around a **Functional Core, Imperative Shell** architecture.

---

## Features

- **Expressive Pwnagotchi-Style Faces**: Custom crisp vector/pixel expressions (`Awake`, `LookingLeft`, `LookingRight`, `Happy`, `Excited`, `Cool`, `Lonely`, `Bored`, `Sleepy`) rendered tear-free via double-buffered `M5Canvas` sprites.
- **Dynamic Status Phrases**: Deterministic pure PRNG selects mood-aware phrases and live scan commentary (`zeroday>`).
- **Non-Blocking WiFi Scanning**: Uses asynchronous `WiFi.scanNetworks(true, true)` so animations and button inputs remain smooth while scanning channels 1–13.
- **Three Switchable Views**:
  1. **Pet Face View**: Classic Pwnagotchi split layout with face, mood badge, word-wrapped status phrase, strongest SSID spotlight, and telemetry bars.
  2. **Nearby WiFi Radar**: Top deduplicated access points sorted by signal strength with 4-bar signal meters, security indicator (`*`), channel, and RSSI.
  3. **Channel Spectrum**: 2.4 GHz channels 1–13 bar histogram with open vs. secured AP counts.
- **Four Display Themes**: `PaperInk` (classic e-ink look), `DarkInk`, `ZeroDayCyan`, and `PhosphorGreen`.
- **Responsive Layout**: Automatically adapts between `240x135` (M5StickC Plus / Plus2) and `160x80` (original M5StickC).

---

## Architecture (Functional Core, Imperative Shell)

All domain logic outside direct hardware library calls uses **pure functional programming**:

- `include/fp.hpp` — Higher-order functional combinators (`fp::pipe`, `fp::map`, `fp::filter`, `fp::foldl`, `fp::sortBy`, `fp::take`, `fp::Overloaded`, and stateless `xorshift32` PRNG threading).
- `include/domain.hpp` — Immutable value structs (`AccessPoint`, `ScanSummary`, `AppState`) and algebraic event sum types (`std::variant<...> AppEvent`).
- `include/wifi_analysis.hpp` — Pure functional pipeline that sanitizes, sorts, deduplicates mesh BSSIDs, builds channel histograms, and summarizes scan results.
- `include/pet_logic.hpp` — Pure Elm/Redux-style reducer `reduce(const AppState&, const AppEvent&) -> AppState`, mood derivation, face mapping, and phrase selection.
- `include/ui_model.hpp` — Pure responsive UI view-model projection `buildUiFrame(const AppState&, ScreenDimensions) -> UiFrame` and word-wrapping.
- `src/main.cpp` — Thin imperative shell interfacing with `M5Unified`, Arduino `WiFi`, and `Serial`.

---

## Hardware Controls

| Control | Action |
| :--- | :--- |
| **Button A (Front `M5`)** | Cycle screen views (`Pet Face` -> `WiFi Radar` -> `Channel Spectrum`) & wake pet |
| **Button B (Side, short press)** | Trigger an immediate WiFi scan |
| **Button B (Side, hold ~0.7s)** | Cycle color themes (`PaperInk` -> `DarkInk` -> `ZeroDayCyan` -> `PhosphorGreen`) |

---

## Getting Started (VSCode + PlatformIO)

1. Open this folder (`Pwnagotchi-ZeroDay`) as your workspace in **VSCode / Antigravity**.
2. Install the recommended **PlatformIO IDE** extension (`platformio.platformio-ide`).
3. Build and upload to your board:
   - **M5StickC / M5StickC Plus**:
     ```bash
     pio run -e m5stick-c --target upload
     ```
   - **M5StickC Plus2**:
     ```bash
     pio run -e m5stick-c-plus2 --target upload
     ```
4. Monitor serial output (`115200` baud):
   ```bash
   pio device monitor -b 115200
   ```

### Running Host Unit Tests (No Hardware Needed)

Because all application logic and UI layout calculations are pure C++17 functions, you can test them directly on your computer with `g++`:

```bash
mkdir -p build
g++ -std=c++17 -Wall -Wextra -Werror -Iinclude test/test_functional_core.cpp -o build/test_functional_core
./build/test_functional_core
```
