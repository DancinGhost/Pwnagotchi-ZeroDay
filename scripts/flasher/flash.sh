#!/usr/bin/env bash
# Simple Linux/macOS Flasher Launcher for pwnagotchi-zeroday
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if command -v python3 &> /dev/null; then
    python3 flash.py
elif command -v python &> /dev/null; then
    python flash.py
else
    echo "Python 3 is required to run the flasher."
    echo "Alternatively, install esptool and run:"
    echo "  esptool.py write_flash 0x0 *factory.bin"
    exit 1
fi
