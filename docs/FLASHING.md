# Flashing Guide — pwnagotchi-zeroday

This guide covers how to easily flash **pwnagotchi-zeroday** to any supported ESP32 hardware:
- **LILYGO T-Deck / T-Deck Plus** (ESP32-S3)
- **M5StickC / M5StickC Plus** (ESP32)
- **M5StickC Plus2** (ESP32-PICO-V3-02)
- **M5Stack Core2** (ESP32)

---

## Method 1: Web Flasher (Recommended — No Software Installation)

You can flash directly from **Google Chrome** or **Microsoft Edge** over WebSerial without installing Python, drivers, or command-line tools.

1. Download the firmware zip for your device from the **[Releases Tab](https://github.com/DancinGhost/pwnagotchi-zeroday/releases)** (e.g. `pwnagotchi-zd-t-deck-plus.zip`) and extract it.
2. Open the official **[Espressif Web Flasher](https://espressif.github.io/esptool-js/)** or **[Adafruit WebSerial ESPTool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/)** in Chrome or Edge.
3. Plug your board into your computer with a high-quality USB-C data cable.
4. Set the **Baud rate** to `921600` (or `115200`).
5. Click **Connect** and select your board's serial port from the pop-up prompt.
6. Under **Flash Address**, enter `0x0000` (or `0x0`).
7. Click **Choose File** and select `pwnagotchi-zd-<board>-factory.bin`.
8. Click **Program** / **Flash**.
9. Once finished, disconnect or hit the physical **RESET** button on your device!

---

## Method 2: One-Click Desktop Flasher (Included in Every Firmware Zip)

Every firmware zip contains a cross-platform helper flasher (`flash.py`, `flash.sh`, `flash.bat`) that auto-detects your serial port and flashes the merged image.

1. Extract the downloaded `pwnagotchi-zd-<board>.zip`.
2. Connect your board via USB.
3. Run the flasher for your operating system:
   - **Linux / macOS**: Open a terminal in the folder and run:
     ```bash
     chmod +x flash.sh
     ./flash.sh
     ```
   - **Windows**: Double-click `flash.bat` (or open command prompt and run `python flash.py`).
4. The flasher detects the connected board and flashes the factory binary automatically.
5. Press the physical **RESET** / **Power** button to boot.

---

## Method 3: Manual Command Line Flashing (`esptool`)

If you prefer using `esptool.py` directly:

### Single Merged Factory Binary (Flash at `0x0`)
```bash
# LILYGO T-Deck / T-Deck Plus (ESP32-S3)
esptool.py --chip esp32s3 write_flash -z 0x0 pwnagotchi-zd-t-deck-plus-factory.bin

# M5Stack / M5StickC (ESP32)
esptool.py --chip esp32 write_flash -z 0x0 pwnagotchi-zd-m5stick-c-factory.bin
```

### Separate Partitions (Advanced)
If you prefer flashing individual partition components:
```bash
# ESP32 (M5StickC, M5StickC Plus2, M5Stack Core2):
esptool.py --chip esp32 write_flash \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin

# ESP32-S3 (LILYGO T-Deck / T-Deck Plus):
esptool.py --chip esp32s3 write_flash \
  0x0 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin
```
