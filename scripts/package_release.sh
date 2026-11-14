#!/usr/bin/env bash
# scripts/package_release.sh
# Packages locally built binaries for GitHub Releases.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DIST_DIR="$ROOT_DIR/dist_release"

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

echo "=== Packaging pwnagotchi-zeroday Release Assets ==="

# 1. Package Linux Desktop Binaries (prefer CMake build_cmake/, fallback to Makefile build/)
LINUX_BUILD_DIR=""
if [ -f "$ROOT_DIR/build_cmake/zd-linux" ] || [ -f "$ROOT_DIR/build_cmake/zd_sim" ]; then
    LINUX_BUILD_DIR="$ROOT_DIR/build_cmake"
elif [ -f "$ROOT_DIR/build/zd-linux" ] || [ -f "$ROOT_DIR/build/zd_sim" ]; then
    LINUX_BUILD_DIR="$ROOT_DIR/build"
fi

if [ -n "$LINUX_BUILD_DIR" ]; then
    echo "[+] Packaging Linux x86_64 binaries from $LINUX_BUILD_DIR..."
    LINUX_DIR="$(mktemp -d)"
    mkdir -p "$LINUX_DIR/bin"
    [ -f "$LINUX_BUILD_DIR/zd-linux" ] && cp "$LINUX_BUILD_DIR/zd-linux" "$LINUX_DIR/bin/"
    [ -f "$LINUX_BUILD_DIR/zd-gui" ] && cp "$LINUX_BUILD_DIR/zd-gui" "$LINUX_DIR/bin/"
    [ -f "$LINUX_BUILD_DIR/zd_sim" ] && cp "$LINUX_BUILD_DIR/zd_sim" "$LINUX_DIR/bin/"
    cp "$ROOT_DIR/README.md" "$LINUX_DIR/"
    cp "$ROOT_DIR/LICENSE" "$LINUX_DIR/"
    cp "$ROOT_DIR/THIRD_PARTY_NOTICES.md" "$LINUX_DIR/"
    [ -f "$ROOT_DIR/docs/LINUX.md" ] && cp "$ROOT_DIR/docs/LINUX.md" "$LINUX_DIR/"
    tar -czf "$DIST_DIR/pwnagotchi-zd-linux-x86_64.tar.gz" -C "$LINUX_DIR" .
    rm -rf "$LINUX_DIR"
fi

# 2. Package Web Cyberdeck
if [ -d "$ROOT_DIR/platforms/web" ]; then
    echo "[+] Packaging Web Cyberdeck..."
    (cd "$ROOT_DIR/platforms/web" && zip -r "$DIST_DIR/pwnagotchi-zd-web.zip" index.html styles.css js/)
    zip -j "$DIST_DIR/pwnagotchi-zd-web.zip" "$ROOT_DIR/LICENSE" "$ROOT_DIR/THIRD_PARTY_NOTICES.md"
fi

# 3. Package Any Pre-Built ESP32 Firmwares in .pio/build
for env_dir in "$ROOT_DIR/.pio/build"/*; do
    if [ -d "$env_dir" ] && [ -f "$env_dir/firmware.bin" ]; then
        target="$(basename "$env_dir")"
        echo "[+] Packaging firmware for $target..."
        TEMP_FW="$(mktemp -d)"
        cp "$env_dir/firmware.bin" "$TEMP_FW/"
        [ -f "$env_dir/bootloader.bin" ] && cp "$env_dir/bootloader.bin" "$TEMP_FW/"
        [ -f "$env_dir/partitions.bin" ] && cp "$env_dir/partitions.bin" "$TEMP_FW/"
        # Copy flasher utilities and docs
        if [ -d "$ROOT_DIR/scripts/flasher" ]; then
            cp "$ROOT_DIR/scripts/flasher"/* "$TEMP_FW/"
        fi
        [ -f "$ROOT_DIR/docs/FLASHING.md" ] && cp "$ROOT_DIR/docs/FLASHING.md" "$TEMP_FW/"
        cp "$ROOT_DIR/LICENSE" "$ROOT_DIR/THIRD_PARTY_NOTICES.md" "$TEMP_FW/"
        (cd "$TEMP_FW" && zip -r "$DIST_DIR/pwnagotchi-zd-${target}.zip" ./*)
        rm -rf "$TEMP_FW"
    fi
done

# 4. Generate SHA256 Checksums
if [ -n "$(ls -A "$DIST_DIR" 2>/dev/null)" ]; then
    echo "[+] Generating SHA256SUMS.txt..."
    (cd "$DIST_DIR" && sha256sum * > SHA256SUMS.txt)
    echo "=== Packaging complete. Artifacts in dist_release/: ==="
    ls -lh "$DIST_DIR"
else
    echo "[-] No artifacts found to package. Build targets first."
fi
