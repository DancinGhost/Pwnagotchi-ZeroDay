#!/usr/bin/env python3
"""
pwnagotchi-zeroday Easy Board Flasher
Cross-platform tool to flash precompiled factory binaries to ESP32 / ESP32-S3 boards.
"""

import glob
import os
import subprocess
import sys

def get_serial_ports():
    """Detect available serial / USB ports on Windows, macOS, and Linux."""
    ports = []
    if sys.platform.startswith('win'):
        try:
            import serial.tools.list_ports
            ports = [p.device for p in serial.tools.list_ports.comports()]
        except ImportError:
            # Fallback if pyserial not installed: test COM ports 1..32
            import serial
            for i in range(1, 33):
                port = f"COM{i}"
                try:
                    s = serial.Serial(port)
                    s.close()
                    ports.append(port)
                except Exception:
                    pass
    elif sys.platform.startswith('darwin'):
        ports = glob.glob('/dev/cu.usbserial*') + glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/cu.wchusbserial*')
    else:
        # Linux
        ports = glob.glob('/dev/ttyUSB*') + glob.glob('/dev/ttyACM*')
    return sorted(list(set(ports)))

def ensure_esptool():
    """Ensure esptool is installed and accessible."""
    # Check if esptool.py is in PATH
    try:
        subprocess.run(["esptool.py", "version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        return ["esptool.py"]
    except (subprocess.SubprocessError, FileNotFoundError):
        pass

    # Check python -m esptool
    try:
        subprocess.run([sys.executable, "-m", "esptool", "version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        return [sys.executable, "-m", "esptool"]
    except (subprocess.SubprocessError, FileNotFoundError):
        pass

    print("[*] esptool not detected. Installing via pip...")
    try:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "--upgrade", "esptool"])
        return [sys.executable, "-m", "esptool"]
    except Exception as err:
        print(f"[!] Failed to auto-install esptool: {err}")
        print("    Please install esptool manually: pip install esptool")
        sys.exit(1)

def main():
    print("==================================================")
    print("      pwnagotchi-zeroday Easy Board Flasher       ")
    print("==================================================")

    # 1. Locate factory binary in current directory
    bins = glob.glob("*factory*.bin")
    if not bins:
        bins = glob.glob("*.bin")

    if not bins:
        print("[!] Error: No .bin firmware found in this folder.")
        sys.exit(1)

    factory_bin = bins[0]
    for b in bins:
        if "factory" in b:
            factory_bin = b
            break

    print(f"[*] Target Firmware: {factory_bin}")

    # Determine chip family from filename or prompt
    chip_arg = []
    if "t-deck" in factory_bin.lower() or "s3" in factory_bin.lower():
        chip_arg = ["--chip", "esp32s3"]
        print("[*] Detected Board: ESP32-S3 (LILYGO T-Deck / T-Deck Plus)")
    elif "m5" in factory_bin.lower() or "stick" in factory_bin.lower() or "core2" in factory_bin.lower():
        chip_arg = ["--chip", "esp32"]
        print("[*] Detected Board: ESP32 (M5StickC / Plus2 / Core2)")

    # 2. Find serial ports
    ports = get_serial_ports()
    selected_port = None

    if not ports:
        print("[*] No serial port detected automatically.")
        print("    Please plug in your board via USB.")
        manual = input("    Enter serial port name (or press Enter to let esptool auto-probe): ").strip()
        if manual:
            selected_port = manual
    elif len(ports) == 1:
        selected_port = ports[0]
        print(f"[*] Found 1 USB device: {selected_port}")
    else:
        print("[*] Multiple USB ports detected:")
        for idx, p in enumerate(ports, 1):
            print(f"    [{idx}] {p}")
        choice = input(f"Select port [1-{len(ports)}] (default 1): ").strip()
        if choice.isdigit() and 1 <= int(choice) <= len(ports):
            selected_port = ports[int(choice) - 1]
        else:
            selected_port = ports[0]
        print(f"[*] Selected port: {selected_port}")

    # 3. Ensure esptool is ready
    esptool_cmd = ensure_esptool()

    # 4. Construct flashing command
    # Unified factory image is flashed at offset 0x0
    cmd = esptool_cmd + chip_arg
    if selected_port:
        cmd += ["--port", selected_port]
    cmd += ["--baud", "921600", "write_flash", "-z", "0x0", factory_bin]

    print("\n[*] Starting flash process...")
    print(f"    Running: {' '.join(cmd)}\n")

    try:
        ret = subprocess.run(cmd)
        if ret.returncode != 0:
            # Fallback to lower baud rate if 921600 failed
            print("\n[!] High-speed flash failed. Retrying at 115200 baud...")
            cmd[cmd.index("921600")] = "115200"
            ret = subprocess.run(cmd)

        if ret.returncode == 0:
            print("\n==================================================")
            print(" [OK] Flashing Complete!")
            print(" Press the RESET/PWR button on your board to boot")
            print(" pwnagotchi-zeroday!")
            print("==================================================")
        else:
            print("\n[!] Flashing failed. Please check your USB cable and board connection.")
    except Exception as e:
        print(f"\n[!] Error during flashing: {e}")

if __name__ == "__main__":
    main()
