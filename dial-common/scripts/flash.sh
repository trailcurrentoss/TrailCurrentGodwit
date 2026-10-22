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
# so plug in one at a time.
#
#   dial-common/scripts/flash.sh left               build if needed, flash, monitor
#   dial-common/scripts/flash.sh right --no-monitor
#
# Ports are found by USB identity under /dev/serial/by-id, never hardcoded.
set -euo pipefail

usage() { echo "usage: $0 left|right [--no-monitor]" >&2; exit 2; }

case "${1:-}" in
    left)  SIDE=left;  NAME=Left ;;
    right) SIDE=right; NAME=Right ;;
    *)     usage ;;
esac
MONITOR=1
[ "${2:-}" = "--no-monitor" ] && MONITOR=0

cd "$(dirname "$0")/../../dial-$SIDE"

command -v idf.py >/dev/null || { echo "idf.py not found -- source ESP-IDF first: . \$IDF_PATH/export.sh" >&2; exit 1; }

app_port()  { ls /dev/serial/by-id/*Godwit_Dial_${NAME}* 2>/dev/null | head -n1 || true; }
rom_ports() { ls /dev/serial/by-id/*Espressif_USB_JTAG* 2>/dev/null || true; }

port=""
app="$(app_port)"
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
        exit 1
    fi
    if [ "$count" -gt 1 ]; then
        echo "More than one board is in the ROM loader. Unplug all but the $SIDE dial." >&2
        exit 1
    fi
    port="$(rom_ports)"
    echo "No running $SIDE dial; flashing the board in the ROM loader."
fi

echo "Flashing the $SIDE dial via $port"
idf.py -p "$port" flash
[ "$MONITOR" -eq 1 ] || exit 0

# After the flash the app starts TinyUSB, so the monitor must follow the
# dial to its new port -- the ROM port is gone by then.
echo "Waiting for the firmware's serial port..."
for _ in $(seq 1 50); do
    app="$(app_port)"
    [ -n "$app" ] && exec idf.py -p "$app" monitor --no-reset
    sleep 0.2
done
echo "Firmware port did not appear; flash completed anyway." >&2
