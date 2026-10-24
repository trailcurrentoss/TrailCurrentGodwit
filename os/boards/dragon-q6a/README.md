# Godwit on the Radxa Dragon Q6A

How to build the Godwit image for a Radxa Dragon Q6A (Qualcomm QCS6490) and
put it on the board. Run every command on this page from the repository's
`os/` folder.

## What you need

- A Dragon Q6A with the storage you'll boot from: an M.2 2230 NVMe SSD, an
  eMMC module, or a microSD card
- Its 12 V USB PD Type-C power adapter
- For flashing NVMe or eMMC: a **USB-A to USB-A** data cable
- An HDMI display with touch, or a USB mouse
- The build machine prepared as described in
  [Prepare the build machine](../../README.md#prepare-the-build-machine-once)
- SPI firmware 2026-01-20 or newer on the board (see
  [Updating the SPI firmware](flashing-edl.md#updating-the-spi-firmware))

## 1. Build

```sh
scripts/setup.sh --board dragon-q6a
source agl/build-dragon-q6a/agl-init-build-env
bitbake godwit-image
```

The first `setup.sh` run asks whether to reuse another build's files; see
[Create a build directory](../../README.md#create-a-build-directory). The
image lands in
`agl/build-dragon-q6a/tmp/deploy/images/dragon-q6a/godwit-image-dragon-q6a.rootfs.wic.zst`.

`source` changes into the build directory. Before running the next step's
commands, go back to `os/`:

```sh
cd ../..
```

## 2. Flash

Pick one of the two ways.

### To NVMe or eMMC, over EDL (recommended)

EDL mode lets the build machine write straight to the board's onboard
storage over USB. **The full steps, with checks and troubleshooting, are in
[flashing-edl.md](flashing-edl.md).** In short:

1. **Stage the new image.** `flash-edl.sh` writes the image in
   `boards/dragon-q6a/flash/`, not the one in the build folder, so copy it
   there after every build
   ([Re-staging after a rebuild](flashing-edl.md#re-staging-after-a-rebuild)).
   On a fresh clone, the EDL tools need staging there once too.
2. **Put the board in EDL mode.** With power off, connect the USB-A port to
   the build machine, hold the EDL button, connect power, then release the
   button. `lsusb | grep 9008` should show the board.
3. **Check the target.** This only reads the drive:

   ```sh
   sudo boards/dragon-q6a/flash-edl.sh --target nvme --check     # or: --target emmc
   ```

4. **Write the image.** You're asked to type the target name to confirm:

   ```sh
   sudo boards/dragon-q6a/flash-edl.sh --target nvme             # or: --target emmc
   ```

5. Remove power, unplug the USB-A cable, and power on without holding the
   button.

### To a microSD card or USB drive

```sh
sudo bmaptool copy agl/build-dragon-q6a/tmp/deploy/images/dragon-q6a/godwit-image-dragon-q6a.rootfs.wic.zst /dev/sdX
```

Replace `/dev/sdX` with the card (check with `lsblk`; everything on it is
erased). Insert it and power on. The firmware boots USB first, then SD,
NVMe, eMMC and UFS.

## 3. First boot

- The Radxa logo shows, then the Godwit HMI comes up full screen on HDMI.
  It starts before the network, Bluetooth and audio, which follow in the
  background.
- On first boot `godwit-growroot` grows the root partition to fill the
  drive, while the HMI runs.
- **Navigation needs a map bundle.** The map says "No map installed" until
  one is copied to `/data/maps`; see [../../docs/maps.md](../../docs/maps.md).
- The boot is quiet with no menu. Press a key on the serial console or a USB
  keyboard during boot for the systemd-boot menu.
- **Logging in:** the default image includes `agl-devel`, so `root` has no
  password. Use the serial console (`ttyMSM0` at 115200 baud, header pins 8
  TX and 10 RX) or SSH once Ethernet has a DHCP address:
  `ssh root@<board-ip>`.

If it doesn't come up, see
[If the board doesn't boot](flashing-edl.md#if-the-board-doesnt-boot), and
start with `journalctl -u godwit-hmi -b`.

## Wiring

The GNSS receiver goes on header pins 1 (3V3), 3 (SDA), 5 (SCL) and 6
(GND): [../../../DOCS/boards/dragon-q6a/godwit-header-pinout.png](../../../DOCS/boards/dragon-q6a/godwit-header-pinout.png).
The encoder dials plug into USB ports.

## How it boots

The Q6A ships with UEFI firmware in its SPI NOR flash. That firmware loads a
standard EFI bootloader from microSD, USB, NVMe, eMMC or UFS. So this port
skips Qualcomm's Android-style boot images and `meta-qcom` and builds an
ordinary GPT disk image:

| Partition | Contents |
|---|---|
| ESP (vfat, 128 MB) | systemd-boot, kernel `Image`, `qcs6490-radxa-dragon-q6a.dtb` |
| root (ext4) | AGL root filesystem |

systemd-boot passes the DTB to the kernel. The firmware can only pick a DTB
itself for 6.17 and 6.18 kernels, and this image runs 7.0.

## Layer: `meta-dragon-q6a`

| Piece | Source |
|---|---|
| Machine `dragon-q6a` | `conf/machine/dragon-q6a.conf`, armv8.2-a crypto tune |
| Kernel `linux-radxa-qcom` 7.0.11 | [radxa/kernel](https://github.com/radxa/kernel) `linux-7.0.11`, the branch Radxa's own OS images build. Uses the in-tree `radxa_qcom_7_0_defconfig`, AGL's fragments and `q6a-yocto.cfg`. |
| DSP, GPU and VPU firmware | upstream `linux-firmware` packages (the Q6A ADSP and CDSP blobs are upstream) |
| GPU zap shader `qcom/qcs6490/a660_zap.mbn` | `dragon-q6a-firmware`, taken from [radxa-pkg/radxa-firmware](https://github.com/radxa-pkg/radxa-firmware) |
| GPU userspace | `mesa.bbappend` enables freedreno (GL/GLES) and turnip (Vulkan). OE-core only turns on GPU drivers by default on x86. |
| Display orientation | `WESTON_DISPLAYS = "hdmi-a-1-0"`: landscape, where AGL defaults to portrait |
| Disk layout | `files/wic/dragon-q6a.wks.in` |
| AGL machine template | `templates/machine/dragon-q6a` |

Mainline Linux has a Q6A device tree, but it has no HDMI, USB 3/Type-C or
UFS. Radxa's tree adds all three, which is why this layer uses it.

### Changes to the Godwit HMI (`dynamic-layers/godwit/`)

| Change | Why |
|---|---|
| `70-godwit-gnss.rules` | `0002-arm64-dts-qcom-dragon-q6a-Enable-I2C6-for-GNSS.patch` turns on I2C6 (`998000.i2c`), header pins 3 and 5. Its bus number depends on probe order, so this rule names it `/dev/gnss-i2c`, which the HMI reads. |
| `70-godwit-audio.rules` | The on-board codec's headphone amplifiers (`HPHL`, `HPHR`, the 3.5 mm output) power up at +6 dB, which clips speech. The rule sets them to -6 dB (step 16 of 24) with `amixer` when the card appears; volume is then set per audio role in software. |

## Known gaps

- **Wi-Fi/BT**: the onboard AIC8800 (on USB) uses Radxa's out-of-tree
  [aic8800](https://github.com/radxa-pkg/aic8800) driver, packaged as
  `aic8800-usb` with Radxa's patch series and firmware. ConnMan manages
  Wi-Fi and Bluetooth power; the HMI's Settings screen drives Wi-Fi through
  ConnMan and phone pairing through BlueZ. BlueZ is configured to show the
  head unit to phones as "Godwit", a car audio device, and to run classic
  Bluetooth only (`ControllerMode = bredr`). In dual mode an iPhone stalls on
  the Low Energy key exchange the kernel starts after pairing, and reports
  the pairing as failed. If something does not work, check
  `lsmod | grep aic`, `dmesg | grep -i aic`, `connmanctl technologies`,
  `bluetoothctl show` and `journalctl -u godwit-hmi -b`.
- **Audio**: the 3.5 mm jack (WCD9385 codec) uses the upstream
  `QCS6490-Radxa-Dragon-Q6A` UCM profile. The layer backports two newer
  upstream fixes: the card-name link and the wcd938x headphone mode. USB
  audio devices work as-is. If PipeWire comes up with only a dummy output,
  the codec probed after WirePlumber's first scan. Restarting WirePlumber
  fixes it.
- **Bring-up arguments**: `clk_ignore_unused pd_ignore_unused` are on the
  kernel command line. Remove them once every peripheral works.
