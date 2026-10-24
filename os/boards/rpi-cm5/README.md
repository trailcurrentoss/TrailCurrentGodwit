# Godwit on the Raspberry Pi CM5

How to build the Godwit image for a Raspberry Pi Compute Module 5 on a base
board with an M.2 NVMe slot, and write it to the NVMe drive. Tested on a
Waveshare CM5 base board. Run every command on this page from the
repository's `os/` folder.

AGL supports the Pi 5 directly (machine `raspberrypi5`, layer
`meta-raspberrypi`), and that machine already ships the CM5 device trees
(`bcm2712-rpi-cm5-cm5io.dtb` and others). This board's layer,
`meta-rpi-cm5`, only holds what the CM5 and the head unit need on top.

## What you need

- A CM5 on its base board, with an M.2 NVMe drive fitted to the board, and
  a power supply for the board. The CM5 can have eMMC or not (Lite); the
  image ignores the eMMC.
- A USB cable from the base board's flashing port (normally the USB-C port)
  to the build machine. Check the board's wiki if you're unsure which port.
- An HDMI display with touch, or a USB mouse
- The build machine prepared as described in
  [Prepare the build machine](../../README.md#prepare-the-build-machine-once),
  including `libusb-1.0-0-dev` and `pkg-config`

## 1. Build

```sh
scripts/setup.sh --board rpi-cm5
source agl/build-raspberrypi5/agl-init-build-env
bitbake godwit-image
```

The first `setup.sh` run asks whether to reuse another build's files; see
[Create a build directory](../../README.md#create-a-build-directory). The
image lands in `agl/build-raspberrypi5/tmp/deploy/images/raspberrypi5/`,
named `godwit-image-raspberrypi5.rootfs-<date>.wic.zst`.

`source` changes into the build directory. Before running the next step's
commands, go back to `os/`:

```sh
cd ../..
```

AGL's Flutter IVI demo builds from the same directory with
`bitbake agl-ivi-demo-flutter`. It draws in portrait (1080x1920).

## 2. Flash the NVMe drive

The NVMe drive is written from the build machine over USB, without taking
it out of the board.

1. **Put the CM5 in USB boot mode.** Power off the base board. Fit the BOOT
   jumper, or hold the BOOT button while you power on. (Raspberry Pi calls
   this signal nRPIBOOT.) The CM5 then waits for a computer to load it over
   USB instead of booting normally.
2. **Connect it.** Plug the board's flashing port into the build machine,
   then power the board on. Check that the computer sees it:

   ```sh
   lsusb | grep 0a5c
   ```

   You should see `Broadcom Corp. BCM2712D0 Boot`. If nothing shows, see
   Troubleshooting below.
3. **Check that the NVMe drive appears.** This only reads the drive:

   ```sh
   sudo boards/rpi-cm5/flash-cm5.sh --check
   ```

   On first use this downloads and builds `rpiboot` into `tools/usbboot`.
   It then loads Raspberry Pi's mass-storage program onto the CM5, which
   makes the NVMe drive appear on the build machine as a USB disk, and
   prints it:

   ```
   NAME   SIZE VENDOR  FSTYPE LABEL
   sda  119.2G nvme0n1
   ```

   `nvme0n1` in the VENDOR column is how you know it's the NVMe drive and
   not the CM5's eMMC (`mmcblk0`) or one of the build machine's own disks.
   Check the size matches your drive (a "128 GB" drive shows as 119.2G).
4. **Write the image:**

   ```sh
   sudo boards/rpi-cm5/flash-cm5.sh
   ```

   It finds the newest `godwit-image` build and the NVMe drive, shows both,
   and asks before erasing anything. Typing `y` erases every partition on
   the drive, including any other OS on it. While it writes, a byte counter
   climbs to the image's size (a few GB); writing takes 2 to 5 minutes and
   the script ends with `Done`. If it stops early or reports
   `0 bytes copied`, nothing useful was written; check the error above it
   and run it again. To write the Flutter demo instead, add
   `--name agl-ivi-demo-flutter`.
5. **Boot it.** Power off, remove the BOOT jumper, connect the display to
   the first HDMI port, and power on.

Unlike the Q6A's EDL script, this one needs no staging: it always takes the
newest image straight from the build folder.

## 3. First boot

- The HMI comes up full screen in landscape within a few seconds of the
  display being detected. It is laid out for 1024x600 and scaled to fit, so
  a 1920x1080 monitor works. It starts before the network, Bluetooth and
  audio, which follow in the background.
- On first boot `godwit-growroot` grows the root partition to fill the NVMe
  drive, while the HMI runs.
- **Navigation needs a map bundle.** The map is empty until one is copied to
  `/data/maps`; see [../../docs/maps.md](../../docs/maps.md).
- **Logging in:** the default image includes `agl-devel`, so `root` has no
  password. Use the serial console (GPIO 14/15 UART, 115200 baud) or SSH
  once Ethernet has a DHCP address: `ssh root@<board-ip>`.

## Wiring

The GNSS receiver goes on the same header pins as on the Q6A: 1 (3V3),
3 (SDA), 5 (SCL) and 6 (GND). On the CM5 those are I2C1 on the RP1 chip.
The encoder dials plug into USB ports.

## What `meta-rpi-cm5` changes

`board.env` turns on two AGL features from this layer.

**`cm5-nvme`: boot from NVMe**

| Change | Why |
|---|---|
| The Pi firmware boots the kernel directly (`RPI_USE_U_BOOT = "0"`) | AGL normally boots the Pi through U-Boot, whose boot script reads only the SD/eMMC slot and which has no PCIe driver for the CM5's chip, so it cannot see NVMe. The firmware boots NVMe itself. |
| Partition layout `files/wic/cm5-nvme.wks` on `nvme0n1` | `/boot` is mounted by its partition UUID. wic would otherwise write `/dev/nvme0n11` for it, which doesn't exist. |
| `root=/dev/nvme0n1p2` on the kernel command line | Root filesystem on the NVMe drive |
| `dtparam=pciex1` in `config.txt` | Turns on the PCIe lane the M.2 slot uses |

**`cm5-godwit`: the Godwit HMI on the Pi**

Turns on the `godwit` feature, then adapts it to the Pi. The HMI changes are
in `meta-rpi-cm5/dynamic-layers/godwit/`.

| Change | Why |
|---|---|
| `godwit-kms-config`, run before the HMI starts | A Pi 5/CM5 has two DRM cards: v3d (rendering only) and vc4 (HDMI). Qt's eglfs opens the first, which may be v3d. This points it at the card with a connected display. |
| `ENABLE_I2C = "1"`, `i2c-dev` loaded at boot, and `70-godwit-gnss.rules` | Turns on the header's I2C bus (`1f00074000.i2c`) and names it `/dev/gnss-i2c`, which the HMI reads. |

## Troubleshooting

| Symptom | Fix |
|---|---|
| `lsusb` shows no `0a5c:2712` | Check the BOOT jumper is fitted *before* power-on, that the cable carries data (some USB-C cables are power-only), and that you're on the flashing port. |
| Kernel log (`sudo dmesg`) shows `error -71` or `device not accepting address` | A USB port or cable problem. Try another port on the computer, preferably one directly on the machine rather than a hub. |
| "no NVMe drive appeared" | Power off and check the drive is seated in the M.2 slot and screwed down. Some base boards need a switch or jumper to power the M.2 slot; check the board's wiki. |
| "no godwit-image image in ..." | That image hasn't been built for `raspberrypi5`. Build it, or pass `--image FILE`. |
| Rainbow screen or a Raspberry Pi boot diagnostics screen, then retries | The bootloader isn't trying NVMe. Its `BOOT_ORDER` setting (in the CM5's EEPROM) must include `6` (NVMe), for example `0xf461`. The diagnostics screen shows the current value. To change it, see Raspberry Pi's [bootloader configuration](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#BOOT_ORDER) docs; on a CM5 it's updated with `rpiboot -d recovery5` from `tools/usbboot`. |
| An older OS on the CM5's eMMC boots instead | The bootloader tries eMMC before NVMe. Put `6` first in `BOOT_ORDER` (for example `0xf416`), or erase the eMMC. |
| Boot stops at `Waiting for root device /dev/nvme0n1p2` | The kernel can't see the drive. Check the serial console for PCIe errors, and that `config.txt` on the boot partition has `dtparam=pciex1`. |
| Black screen, boot messages stop | Log in on the serial console or SSH and run `journalctl -u godwit-hmi -b`. "no connected display found" means the HDMI display wasn't detected: check the cable and that it's in the first HDMI port. |
| No GNSS fix, `/dev/gnss-i2c` missing | Check the receiver is wired to header pins 3 and 5 and shows up with `i2cdetect -y -r 1`. |

## Known gaps

- **Audio**: the CM5 has no analogue audio output. Sound goes to HDMI or a
  USB audio device; there is no board-specific audio setup yet.
- **Wi-Fi/BT**: only on CM5 modules with the wireless option. Not yet
  tested with the HMI's Settings and Phone screens.
- **Wiring diagram**: there is no CM5 header diagram yet. The GNSS pins are
  the same physical pins as on the Q6A.
