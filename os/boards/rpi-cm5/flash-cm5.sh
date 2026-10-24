#!/usr/bin/env bash
# Write the Godwit image to the NVMe drive on a Raspberry Pi CM5 over USB (rpiboot).
#
#   sudo boards/rpi-cm5/flash-cm5.sh --check     expose the NVMe drive and print its layout only
#   sudo boards/rpi-cm5/flash-cm5.sh             write the newest godwit-image
#   sudo boards/rpi-cm5/flash-cm5.sh --name agl-ivi-demo-flutter
#
# The CM5 must be in USB boot mode first (see README.md beside this script).
# rpiboot loads Raspberry Pi's mass-storage gadget, which makes the CM5's NVMe drive (and
# eMMC, if fitted) appear here as USB disks. The gadget puts the source
# device's name in the disk's vendor field, so the NVMe drive is the USB
# disk whose vendor reads "nvme0n1".
#
# Options:
#   --name IMAGE    image recipe to take the newest build of (default
#                   godwit-image)
#   --image FILE    .wic, .wic.bz2, .wic.xz or .wic.zst  (FLASH_IMAGE)
#   --device DEV    skip detection and write to DEV      (FLASH_DEVICE)
#   --usbboot DIR   usbboot checkout with rpiboot built   (USBBOOT_DIR)
#   --yes           do not ask before writing
set -euo pipefail

TOP=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
DEPLOY=$TOP/agl/build-raspberrypi5/tmp/deploy/images/raspberrypi5

IMAGE=${FLASH_IMAGE:-}
NAME=godwit-image
DEVICE=${FLASH_DEVICE:-}
USBBOOT=${USBBOOT_DIR:-$TOP/tools/usbboot}
CHECK=0
ASSUME_YES=0

die() { echo "error: $*" >&2; exit 1; }

usage() { sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --name)    NAME=${2:-};    shift 2 ;;
        --image)   IMAGE=${2:-};   shift 2 ;;
        --device)  DEVICE=${2:-};  shift 2 ;;
        --usbboot) USBBOOT=${2:-}; shift 2 ;;
        --check)   CHECK=1; shift ;;
        --yes)     ASSUME_YES=1; shift ;;
        -h|--help) usage ;;
        *) echo "unknown option: $1" >&2; usage 1 ;;
    esac
done

[ "$(id -u)" -eq 0 ] || die "run with sudo (raw USB and block device access)"

if [ "$CHECK" -eq 0 ] && [ -z "$IMAGE" ]; then
    IMAGE=$(ls -t "$DEPLOY/$NAME"-raspberrypi5*.wic* 2>/dev/null \
            | grep -Ev '\.(bmap|sha256sum)$' | head -1 || true)
    [ -n "$IMAGE" ] || die "no $NAME image in $DEPLOY; build it, or pass --name or --image"
fi
[ "$CHECK" -eq 1 ] || [ -f "$IMAGE" ] || die "image not found at $IMAGE"
# The deploy directory's unversioned names are symlinks, which zstd and xz
# refuse to read
[ -n "$IMAGE" ] && IMAGE=$(readlink -f "$IMAGE")

# Exposed NVMe drives: USB disks the gadget labelled with vendor nvme*
gadget_nvme() {
    lsblk -dnpo NAME,TRAN,TYPE,VENDOR | awk '$2=="usb" && $3=="disk" && $4 ~ /^nvme/ {print $1}'
}

if [ -z "$DEVICE" ]; then
    DEVICE=$(gadget_nvme)
    if [ -z "$DEVICE" ]; then
        if ! lsusb 2>/dev/null | grep -q '0a5c:2712'; then
            cat >&2 <<'EOF'
error: no CM5 in USB boot mode (USB 0a5c:2712), and no NVMe drive exposed.
  1. Power off the base board
  2. Fit the BOOT jumper (or hold the BOOT button) to select USB boot mode
  3. Connect the board's USB-C flashing port to this computer
  4. Power on, then check: lsusb | grep 0a5c:2712
If the kernel log shows "error -71", try another port or cable.
EOF
            exit 1
        fi

        # rpiboot is built from source on first use
        if [ ! -x "$USBBOOT/rpiboot" ]; then
            [ -d "$USBBOOT" ] || git clone --depth 1 https://github.com/raspberrypi/usbboot "$USBBOOT"
            make -C "$USBBOOT" rpiboot || die "rpiboot build failed (needs libusb-1.0-0-dev, pkg-config)"
        fi
        "$USBBOOT/rpiboot" -d "$USBBOOT/mass-storage-gadget64"

        echo "Waiting for the NVMe drive to appear..."
        for _ in $(seq 30); do
            DEVICE=$(gadget_nvme)
            [ -n "$DEVICE" ] && break
            sleep 1
        done
        [ -n "$DEVICE" ] || die "no NVMe drive appeared; check it is seated in the base board's M.2 slot"
    fi
    [ "$(echo "$DEVICE" | wc -l)" -eq 1 ] || die "several NVMe drives exposed; pass --device: $DEVICE"
fi

[ -b "$DEVICE" ] || die "$DEVICE is not a block device"
[ "$(lsblk -dno TRAN "$DEVICE")" = "usb" ] || die "$DEVICE is not a USB disk; refusing"

echo
lsblk -o NAME,SIZE,VENDOR,FSTYPE,LABEL "$DEVICE"
if [ "$CHECK" -eq 1 ]; then
    echo
    echo "The CM5's NVMe drive is $DEVICE. To write the image:"
    echo "  sudo $0"
    exit 0
fi

echo
echo "Image:  $IMAGE"
echo "Target: $DEVICE ($(lsblk -dno SIZE "$DEVICE"), the CM5's NVMe drive)"
if [ "$ASSUME_YES" -eq 0 ]; then
    read -rp "Erase $DEVICE and write the image? [y/N] " ans
    [ "${ans,,}" = "y" ] || die "aborted"
fi

for part in $(lsblk -lnpo NAME "$DEVICE" | tail -n +2); do
    umount "$part" 2>/dev/null || true
done

case "$IMAGE" in
    *.wic.bz2) DECOMP=(bzip2 -dc) ;;
    *.wic.xz)  DECOMP=(xz -dc) ;;
    *.wic.zst) DECOMP=(zstd -dc) ;;
    *.wic)     DECOMP=(cat) ;;
    *) die "unknown image type: $IMAGE" ;;
esac

if command -v bmaptool >/dev/null && [ -f "${IMAGE%.*}.bmap" -o -f "$IMAGE.bmap" ]; then
    bmaptool copy "$IMAGE" "$DEVICE" || die "bmaptool failed; the drive may be partly written"
else
    "${DECOMP[@]}" "$IMAGE" | dd of="$DEVICE" bs=4M conv=fsync oflag=direct status=progress \
        || die "writing failed; the drive may be partly written"
fi
sync
echo
echo "Done. Power off, remove the BOOT jumper, and power on."
