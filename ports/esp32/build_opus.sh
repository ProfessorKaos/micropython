#!/usr/bin/env bash
# build_opus.sh — Build MicroPython with Opus encoder for ESP32-S3
#
# Usage:
#   ./build_opus.sh              # build only
#   ./build_opus.sh flash        # build + flash
#   ./build_opus.sh clean        # wipe build dir
#
# Output: build-OPUS/firmware.bin  (ready to flash at offset 0x0)
#
# Flash command (if not using 'flash' argument):
#   python -m esptool --chip esp32s3 -p /dev/ttyUSB0 -b 460800 \
#     --before default_reset --after hard_reset write_flash 0x0 build-OPUS/firmware.bin

set -e

BOARD=ESP32_GENERIC_S3
VARIANT=OPUS
BUILD_DIR=build-OPUS
PORT=${PORT:-/dev/ttyUSB0}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ABS_BUILD="$SCRIPT_DIR/$BUILD_DIR"

# Source ESP-IDF if not already active
if [ -z "$IDF_PATH" ]; then
    if [ -f "$HOME/esp-idf/export.sh" ]; then
        source "$HOME/esp-idf/export.sh"
    else
        echo "ERROR: IDF_PATH not set and ~/esp-idf/export.sh not found."
        exit 1
    fi
fi

# Ensure required submodules are present (safe to re-run if already init'd)
MICROPYTHON_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
echo "==> Checking required submodules ..."
git -C "$MICROPYTHON_ROOT" submodule update --init \
    lib/berkeley-db-1.xx \
    lib/micropython-lib \
    lib/mbedtls \
    lib/tinyusb \
    lib/lwip

case "${1:-build}" in
    clean)
        echo "==> Cleaning $BUILD_DIR ..."
        rm -rf "$ABS_BUILD"
        echo "Done."
        ;;

    flash)
        # Build first, then flash
        "$0" build
        echo ""
        echo "==> Flashing to $PORT ..."
        python -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
            --before default_reset --after hard_reset \
            write_flash 0x0 "$ABS_BUILD/firmware.bin"
        ;;

    build|*)
        echo "==> Building MicroPython BOARD=$BOARD VARIANT=$VARIANT ..."
        idf.py -B "$ABS_BUILD" \
               -D MICROPY_BOARD=$BOARD \
               -D MICROPY_BOARD_VARIANT=$VARIANT \
               build

        echo ""
        echo "==> Merging into firmware.bin ..."
        idf.py -B "$ABS_BUILD" \
               merge-bin \
               -o "$ABS_BUILD/firmware.bin"

        echo ""
        echo "=== Build complete ==="
        echo "    $ABS_BUILD/firmware.bin"
        echo ""
        echo "Flash with:"
        echo "    PORT=/dev/ttyUSB0 $0 flash"
        echo "  or:"
        echo "    python -m esptool --chip esp32s3 -p /dev/ttyUSB0 -b 460800 \\"
        echo "      --before default_reset --after hard_reset \\"
        echo "      write_flash 0x0 $ABS_BUILD/firmware.bin"
        ;;
esac
