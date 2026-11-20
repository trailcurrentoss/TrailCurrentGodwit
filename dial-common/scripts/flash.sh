#!/bin/bash
# Flash a Godwit dial over its one USB-C cable.
#
# While the firmware runs, the port is a HID device + serial port (TinyUSB)
# and esptool cannot reset it. This script asks the firmware to reboot into
# the ROM loader first -- it writes `bootloader` to the dial's serial port --
# then waits for the ROM's USB-Serial/JTAG port and runs `idf.py flash`.
#
# If the dial is already in the ROM loader (a board fresh from the factory, a
# previous attempt, or BOOT held during reset), it flashes straight away.
# With both dials in the ROM loader at once there is no telling them apart,
# so plug in one at a time. The same goes for two running dials of one side
# (an Elecrow left and a Waveshare left, say): they have the same USB name.
#
#   dial-common/scripts/flash.sh left                   Elecrow CrowPanel, dial-left
#   dial-common/scripts/flash.sh right --no-monitor     Elecrow CrowPanel, dial-right
#   dial-common/scripts/flash.sh waveshare-left         Waveshare knob, dial-waveshare-left
#   dial-common/scripts/flash.sh waveshare-right        Waveshare knob, dial-waveshare-right
#
# The project folder's name works too (dial-left, dial-waveshare-right, ...).
# Ports are found by USB identity under /dev/serial/by-id, never hardcoded.
set -euo pipefail

usage() {
    echo "usage: $0 left|right|waveshare-left|waveshare-right [--no-monitor]" >&2
    exit 2
}

PROJECT="${1:-}"
PROJECT="${PROJECT#dial-}"
case "$PROJECT" in
    left|right|waveshare-left|waveshare-right) ;;
    *) usage ;;
esac
SIDE="${PROJECT##*-}"
NAME="$(echo "${SIDE:0:1}" | tr a-z A-Z)${SIDE:1}"
MONITOR=1
[ "${2:-}" = "--no-monitor" ] && MONITOR=0

cd "$(dirname "$0")/../../dial-$PROJECT"

command -v idf.py >/dev/null || { echo "idf.py not found -- source ESP-IDF first: . \$IDF_PATH/export.sh" >&2; exit 1; }

app_ports() { ls /dev/serial/by-id/*Godwit_Dial_${NAME}* 2>/dev/null || true; }
rom_ports() { ls /dev/serial/by-id/*Espressif_USB_JTAG* 2>/dev/null || true; }

port=""
apps="$(app_ports)"
if [ "$(printf '%s\n' "$apps" | grep -c .)" -gt 1 ]; then
    echo "More than one running $SIDE dial is plugged in. Unplug all but the one to flash." >&2
    exit 1
fi
app="$(printf '%s\n' "$apps" | head -n1)"
if [ -n "$app" ]; then
    echo "Asking the $SIDE dial on $app to reboot into the ROM loader..."
    stty -F "$app" 115200 raw -echo 2>/dev/null || true
    printf 'bootloader\n' > "$app"
    for _ in $(seq 1 50); do
        port="$(rom_ports | head -n1)"
        [ -n "$port" ] && break
        sleep 0.2
    done
    if [ -z "$port" ]; then
        echo "The ROM loader did not appear. Hold BOOT while pressing RESET, then re-run." >&2
        exit 1
    fi
else
    count="$(rom_ports | grep -c . || true)"
    if [ "$count" -eq 0 ]; then
        echo "No $SIDE dial found. Plug it in; if it is stuck, hold BOOT while pressing RESET." >&2
        echo "On a Waveshare knob, the switch on the base must be on the ESP32-S3 side." >&2
        exit 1
    fi
    if [ "$count" -gt 1 ]; then
        echo "More than one board is in the ROM loader. Unplug all but the $SIDE dial." >&2
        exit 1
    fi
    port="$(rom_ports)"
    echo "No running $SIDE dial; flashing the board in the ROM loader."
fi

echo "Flashing dial-$PROJECT via $port"
idf.py -p "$port" flash
[ "$MONITOR" -eq 1 ] || exit 0

# After the flash the app starts TinyUSB, so the monitor must follow the
# dial to its new port -- the ROM port is gone by then.
echo "Waiting for the firmware's serial port..."
for _ in $(seq 1 50); do
    app="$(app_ports | head -n1)"
    [ -n "$app" ] && exec idf.py -p "$app" monitor --no-reset
    sleep 0.2
done
echo "Firmware port did not appear; flash completed anyway." >&2
