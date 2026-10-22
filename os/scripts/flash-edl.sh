#!/usr/bin/env bash
# Write the AGL image to a Dragon Q6A's onboard storage over USB in EDL mode.
#
#   sudo scripts/flash-edl.sh --target nvme --check     read the target's GPT only
#   sudo scripts/flash-edl.sh --target nvme             write the image
#
# Targets: nvme, emmc, sd. UFS is refused: it uses 4096-byte sectors and this
# image is built for 512.
#
# Files come from flash/ (see docs/flashing-edl.md). Override with:
#   --image FILE    raw .wic image  (FLASH_IMAGE)
#   --loader FILE   Firehose loader (FLASH_LOADER)
#   --edl FILE      edl-ng binary   (FLASH_EDL)
set -euo pipefail

TOP=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
STAGE=$TOP/flash

IMAGE=${FLASH_IMAGE:-$STAGE/agl-image-weston-dragon-q6a.wic}
LOADER=${FLASH_LOADER:-$STAGE/firmware/prog_firehose_ddr.elf}
EDL=${FLASH_EDL:-$STAGE/tools/edl-ng/linux-x64/edl-ng}
TARGET=""
CHECK=0
ASSUME_YES=0

die() { echo "error: $*" >&2; exit 1; }

usage() { sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --target) TARGET=${2:-}; shift 2 ;;
        --image)  IMAGE=${2:-};  shift 2 ;;
        --loader) LOADER=${2:-}; shift 2 ;;
        --edl)    EDL=${2:-};    shift 2 ;;
        --check)  CHECK=1; shift ;;
        --yes)    ASSUME_YES=1; shift ;;
        -h|--help) usage ;;
        *) echo "unknown option: $1" >&2; usage 1 ;;
    esac
done

case "$TARGET" in
    nvme) MEM=(--memory NVME) ;;
    emmc) MEM=(--memory SDCC --slot 0) ;;
    sd)   MEM=(--memory SDCC --slot 1) ;;
    ufs)  die "UFS uses 4096-byte sectors; this image is built for 512" ;;
    "")   die "choose --target nvme, emmc or sd" ;;
    *)    die "unknown target '$TARGET' (nvme, emmc or sd)" ;;
esac

[ "$(id -u)" -eq 0 ] || die "run with sudo (raw USB access)"
[ -x "$EDL" ]    || die "edl-ng not found at $EDL"
[ -f "$LOADER" ] || die "Firehose loader not found at $LOADER"
[ "$CHECK" -eq 1 ] || [ -f "$IMAGE" ] || die "image not found at $IMAGE"

if ! lsusb 2>/dev/null | grep -q '05c6:9008'; then
    cat >&2 <<'EOF'
error: no board in EDL mode (USB 05c6:9008).
  1. Connect the board's USB 3.1 Type-A port to this computer (A-to-A cable)
  2. With the board unpowered, hold the EDL button
  3. Connect the 12 V PD Type-C power adapter
  4. Release the button once the board is powered
  5. Check with: lsusb | grep 9008
EOF
    exit 1
fi

if [ "$CHECK" -eq 1 ]; then
    echo "Reading the GPT on $TARGET (read-only)..."
    exec "$EDL" --loader "$LOADER" "${MEM[@]}" printgpt
fi

echo "Image:  $IMAGE ($(du -h "$IMAGE" | cut -f1))"
echo "Target: $TARGET, from sector 0"
echo "This overwrites the start of the $TARGET device, including its partition table."
if [ "$ASSUME_YES" -ne 1 ]; then
    read -rp "Type the target name ($TARGET) to continue: " answer
    [ "$answer" = "$TARGET" ] || die "aborted"
fi

"$EDL" --loader "$LOADER" "${MEM[@]}" write-sector 0 "$IMAGE"
echo "Written. Unplug the USB-C cable and power-cycle the board to boot it."
