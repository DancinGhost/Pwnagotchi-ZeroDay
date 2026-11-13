# Third-Party Notices

pwnagotchi-zeroday's own source code is released under the [MIT License](LICENSE).
No third-party source code is vendored into this repository; the components below are
fetched at build time (PlatformIO / system package manager) and are **linked into, or
required by, the distributed binaries**. Each remains under its own license.

## Embedded firmware (ESP32 / ESP32-S3 release zips)

| Component | License | Source |
|---|---|---|
| Arduino core for ESP32 (`framework-arduinoespressif32`) | LGPL-2.1-or-later | https://github.com/espressif/arduino-esp32 |
| ESP-IDF (bootloader, partition table, libraries; incl. FreeRTOS, lwIP, mbedTLS, etc.) | Apache-2.0 (bundled components under their own permissive licenses) | https://github.com/espressif/esp-idf |
| M5Unified (M5Stack targets) | MIT, Copyright (c) 2021 M5Stack | https://github.com/m5stack/M5Unified |
| M5GFX (M5Stack targets) | MIT, Copyright (c) 2021 M5Stack | https://github.com/m5stack/M5GFX |
| LovyanGFX (T-Deck targets) | FreeBSD (BSD-2-Clause); incorporates Adafruit_GFX (BSD) and Adafruit_ILI9341 (MIT); bundled fonts under their own licenses | https://github.com/lovyan03/LovyanGFX |

**LGPL notice (Arduino-ESP32):** the firmware images statically link the Arduino core for
ESP32. The complete corresponding source of this project (MIT) and the upstream library
sources linked above are publicly available, so you may modify the LGPL-covered library and
rebuild/relink the firmware with `pio run -e <env>`.

## Linux desktop binaries (`pwnagotchi-zd-linux-x86_64.tar.gz`)

These are **dynamically** linked against system libraries that are not shipped in the archive:

| Component | License | Source |
|---|---|---|
| Qt 6 Widgets (`zd-gui` only) | LGPL-3.0 (dynamically linked) | https://www.qt.io/ |
| libpcap | BSD-3-Clause | https://www.tcpdump.org/ |
| ncurses | MIT-style (X11) | https://invisible-island.net/ncurses/ |

## Tooling (not redistributed)

The flasher scripts invoke, but do not bundle, [esptool](https://github.com/espressif/esptool)
(GPL-2.0-or-later), installed separately by the user via `pip`.

## Trademarks

"Pwnagotchi" refers to the original open-source project by evilsocket
(https://github.com/evilsocket/pwnagotchi, GPL-3.0); this project is an independent
reimplementation inspired by it and is not affiliated with or endorsed by it. Tamagotchi is a registered trademark of Bandai; M5Stack, LILYGO,
Espressif, and Qt are trademarks of their respective owners. Their use here is
descriptive only.
