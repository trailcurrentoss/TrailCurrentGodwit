#!/usr/bin/env bash
# Write the Godwit image to a Dragon Q6A's onboard storage over USB in EDL mode.
#
#   sudo boards/dragon-q6a/flash-edl.sh --target nvme --check     read the target's GPT only
#   sudo boards/dragon-q6a/flash-edl.sh --target nvme             erase the drive, write the image
#   sudo boards/dragon-q6a/flash-edl.sh --target nvme --update    a new build, keeping data and maps
#
# Targets: nvme, emmc, sd. UFS is refused: it uses 4096-byte sectors and this
# image is built for 512.
#
# Files come from boards/dragon-q6a/flash/ (see flashing-edl.md). Override with:
#   --image FILE    raw .wic image  (FLASH_IMAGE)
#   --loader FILE   Firehose loader (FLASH_LOADER)
#   --edl FILE      edl-ng binary   (FLASH_EDL)
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
STAGE=$HERE/flash

IMAGE=${FLASH_IMAGE:-$STAGE/godwit-image-dragon-q6a.wic}
LOADER=${FLASH_LOADER:-$STAGE/firmware/prog_firehose_ddr.elf}
EDL=${FLASH_EDL:-$STAGE/tools/edl-ng/linux-x64/edl-ng}
TARGET=""
CHECK=0
UPDATE=0
ASSUME_YES=0

die() { echo "error: $*" >&2; exit 1; }

usage() { sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --target) TARGET=${2:-}; shift 2 ;;
        --image)  IMAGE=${2:-};  shift 2 ;;
        --loader) LOADER=${2:-}; shift 2 ;;
        --edl)    EDL=${2:-};    shift 2 ;;
        --check)  CHECK=1; shift ;;
        --update) UPDATE=1; shift ;;
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

# Only boot and root, by name, so the partition table, data and maps stay
if [ "$UPDATE" -eq 1 ]; then
    table=$("$EDL" --loader "$LOADER" "${MEM[@]}" printgpt)
    if ! grep -q godwit-data <<<"$table" || ! grep -q godwit-content <<<"$table"; then
        die "$TARGET has no data and content partitions: it needs a full flash first (no --update)"
    fi
    work=$(mktemp -d "$STAGE/update.XXXXXX")
    trap 'rm -rf "$work"' EXIT
    for name in boot root; do
        line=$(sfdisk -d "$IMAGE" | grep "name=\"$name\"") || die "no $name partition in $IMAGE"
        start=$(sed -E 's/.*start= *([0-9]+).*/\1/' <<<"$line")
        size=$(sed -E 's/.*size= *([0-9]+).*/\1/' <<<"$line")
        dd if="$IMAGE" of="$work/$name" bs=4M iflag=skip_bytes,count_bytes \
            skip=$((start * 512)) count=$((size * 512)) status=none
    done
    echo "Image:  $IMAGE"
    echo "Target: the boot and root partitions on $TARGET. Data and maps stay."
    if [ "$ASSUME_YES" -ne 1 ]; then
        read -rp "Type the target name ($TARGET) to continue: " answer
        [ "$answer" = "$TARGET" ] || die "aborted"
    fi
    "$EDL" --loader "$LOADER" "${MEM[@]}" write-part boot "$work/boot"
    "$EDL" --loader "$LOADER" "${MEM[@]}" write-part root "$work/root"
    echo "Written. Unplug the USB-C cable and power-cycle the board to boot it."
    exit 0
fi

echo "Image:  $IMAGE ($(du -h "$IMAGE" | cut -f1))"
echo "Target: $TARGET, from sector 0"
echo "This erases the $TARGET drive: personal data and maps are gone after its first boot."
if [ "$ASSUME_YES" -ne 1 ]; then
    read -rp "Type the target name ($TARGET) to continue: " answer
    [ "$answer" = "$TARGET" ] || die "aborted"
fi

"$EDL" --loader "$LOADER" "${MEM[@]}" write-sector 0 "$IMAGE"
echo "Written. Unplug the USB-C cable and power-cycle the board to boot it."
