# Godwit on a Raspberry Pi 5 with a Waveshare 10.1" DSI panel

How to build the Godwit image for a Raspberry Pi 5 that boots from an NVMe
drive, drives a Waveshare
[10.1-DSI-TOUCH-B](https://www.waveshare.com/wiki/10.1-DSI-TOUCH-B) panel and
has a Waveshare [2-CH CAN FD HAT](https://www.waveshare.com/wiki/2-CH_CAN_FD_HAT)
for two CAN buses, and write it to the NVMe drive. Run every command on this page from the
repository's `os/` folder.

The Pi 5 has the same chip as the CM5 and boots NVMe the same way, so this
board reuses two features from [`../rpi-cm5`](../rpi-cm5/README.md):
`cm5-nvme` (boot from NVMe) and `cm5-godwit` (the HMI on a Pi). Its own layer,
`meta-rpi5-dsi`, only adds the panel. Both boards build for the same Yocto
machine, `raspberrypi5`, so this one uses its own build directory,
`agl/build-rpi5-dsi`, and the CM5's `agl/build-raspberrypi5` is untouched.

## What you need

- A Raspberry Pi 5 with an NVMe drive on an M.2 HAT, and its power supply
- The Waveshare 10.1-DSI-TOUCH-B panel, connected as below
- The Waveshare 2-CH CAN FD HAT (optional), set up as below
- One way to write the NVMe drive: a microSD card or USB stick with Raspberry
  Pi OS on it, or a USB NVMe enclosure (see step 2)
- The build machine prepared as described in
  [Prepare the build machine](../../README.md#prepare-the-build-machine-once)

## Connecting the panel

| Connection | Where |
|---|---|
| Video, touch and backlight control | The 22-pin "reverse direction" FFC that comes with the panel, from the panel's DSI port to the Pi's **DSI1** connector (the one nearer the Ethernet port) |
| Power | The panel's power header to a **5V** pin (2 or 4) and a **GND** pin (6) on the Pi's 40-pin header. The FFC does not power the panel. It needs at least 0.92 A. |

Without the power wires the panel stays completely dark: its backlight and
touch controllers do not answer, and the kernel log shows
`failed to enable backlight: -121`.

## The CAN FD HAT

The HAT has two MCP2518FD CAN FD controllers on SPI. Leave it in its default
**mode A** (the four 0R resistors on the front in the "A" position):

| HAT terminal | Port in Settings | Controller | Interrupt |
|---|---|---|---|
| CAN_0 | CAN 1 | SPI0, CE0 | GPIO 25 |
| CAN_1 | CAN 2 | SPI1, CE0 | GPIO 24 |

- Set the logic-voltage jumper to **3.3V**.
- Fit the 120 Ω termination jumper on a channel only if the Pi is at one end
  of that bus.
- The HAT sits on the 40-pin header, which the panel's 5V and GND wires use
  too. Keep those connected when fitting it, for example through a stacking
  header.

Each port is set up in the HMI under **Settings › CAN Buses**: what it is
connected to (vehicle bus, TrailCurrent bus or not used), classic CAN or CAN
FD, the bit rate, the CAN FD data rate, and listen-only. A change applies
straight away. The defaults are:

| Port | Connected to | Protocol | Rates | Listen only |
|---|---|---|---|---|
| CAN 1 | TrailCurrent bus | Classic CAN | 500 kbit/s | Off |
| CAN 2 | Vehicle bus | CAN FD | 500 kbit/s, 2 Mbit/s data | On |

The TrailCurrent modules' ESP32 controllers are classic CAN only, so keep the
TrailCurrent bus on classic CAN: a classic controller treats any CAN FD frame
on the wire as an error.

### Is the rate right?

The **Status** row of each port says whether the rate it is set to matches
the bus, from the controller's own counters averaged over three seconds:

| Status | Meaning |
|---|---|
| Receiving · 180 frames/s | Frames decode: the rate is right |
| Receiving · 180 frames/s · errors | Most frames decode, some do not: often a CAN FD data rate or sample point that does not quite match |
| Errors · check the bit rate | There is traffic, but nothing decodes at this rate |
| No traffic | Nothing on the bus: the vehicle is off, or a wire is off |

**Detect Rates** finds the rates for you. It needs traffic on the bus, so have
the vehicle on. The port listens, in listen-only mode so nothing is ever sent,
for two seconds at each bit rate, then at each data rate, and offers the rates
that receive frames without errors; nothing changes until you choose **Use**.
If no bit rate decodes on its own (every frame on the bus switches to a faster
data rate), it tries every pair, which takes under a minute. A data rate can
only be checked if the bus carries frames that switch to it; if none do, the
result says so and the data rate is left as it is.

How it works: the HMI saves the choices to `/var/lib/godwit/can.conf` and asks
`godwit-can.path` to apply them (or to detect rates, through
`/run/godwit/can-detect`). `godwit-can-apply` then sets each interface
up with `ip link`, as root. It also runs at boot and whenever a CAN interface
appears. CAN 1 and CAN 2 are the first two physical CAN interfaces in device
path order, so the port numbers stay with the HAT's terminals whatever the
kernel names them (`can0`, `can1`); the Status row in Settings shows which
name is which. AGL's own CAN network config is told to leave these interfaces
alone (`50-godwit-can.network`).

On the head unit, `candump can0` shows the traffic and
`ip -details -statistics link show can0` the bus state and error counters.
`journalctl -u godwit-can` shows what was applied.

## 1. Build

```sh
scripts/setup.sh --board rpi5-dsi
source agl/build-rpi5-dsi/agl-init-build-env
bitbake godwit-image
```

The image lands in `agl/build-rpi5-dsi/tmp/deploy/images/raspberrypi5/`,
named `godwit-image-raspberrypi5.rootfs-<date>.wic.zst`. Go back to `os/`
afterwards with `cd ../..`.

## 2. Write the NVMe drive

This erases everything on the drive, including the OS it runs now. Unlike the
CM5, a Pi 5 cannot expose its NVMe drive over USB (`rpiboot`), so write it
one of these two ways.

**From the Pi itself, booted from another card.** A Pi 5 boots from the SD
card before NVMe (the default boot order, `0xf461`), so a Raspberry Pi OS card
leaves the NVMe drive free to write.

1. Copy the image and its block map to the Pi while it runs from the card:

   ```sh
   IMG=agl/build-rpi5-dsi/tmp/deploy/images/raspberrypi5/godwit-image-raspberrypi5.rootfs.wic
   scp -L $IMG.zst $IMG.bmap <user>@<pi-address>:
   ```

2. On the Pi, check the NVMe drive is `nvme0n1` and is not the running
   system (no mount points under it), then write it:

   ```sh
   lsblk -o NAME,SIZE,MODEL,MOUNTPOINTS
   sudo apt install bmap-tools
   sudo bmaptool copy godwit-image-raspberrypi5.rootfs.wic.zst /dev/nvme0n1
   ```

3. Power off, take the card out, and power on.

**In a USB NVMe enclosure.** Take the drive out, put it in the enclosure on
the build machine, find it with `lsblk`, and write it with
`sudo bmaptool copy <image>.wic.zst /dev/sdX`.

## 3. First boot

- `godwit-preinit` divides the drive on first boot, as on the CM5: system,
  personal data and maps each get a partition.
- The HMI comes up in landscape. The panel's native mode is 720x1280 portrait,
  so the HMI turns itself with `GODWIT_ROTATION=90`, set in the
  `godwit-hmi.service.d/rpi5-dsi-rotation.conf` drop-in. Touch follows the
  rotation. If the picture is upside down for the way the panel is mounted,
  change it to `270`.
- Logging in: `root` has no password (`agl-devel`). Use SSH once Ethernet has
  a DHCP address, or the serial console (GPIO 14/15, 115200 baud).
- Navigation needs a map bundle in `/data/maps`; see
  [../../docs/maps.md](../../docs/maps.md).

## What `meta-rpi5-dsi` changes

| Change | Why |
|---|---|
| `overlays/vc4-kms-dsi-waveshare-panel-v2.dtbo` added to `RPI_KERNEL_DEVICETREE_OVERLAYS` | meta-raspberrypi installs only a short list of overlays, without this one |
| `dtoverlay=vc4-kms-dsi-waveshare-panel-v2,10_1_inch_b` in `config.txt` | Tells the kernel the panel is on DSI1. Add `,dsi0` to use the other connector. |
| `GODWIT_ROTATION=90` for `godwit-hmi` | The panel is portrait; the HMI is laid out for landscape |

The `rpi-can-fd-hat` feature, in the same layer, adds the CAN FD HAT:

| Change | Why |
|---|---|
| `dtparam=spi=on`, `dtoverlay=spi1-3cs` and two `dtoverlay=mcp251xfd` lines in `config.txt`, with `spi1-3cs.dtbo` and `mcp251xfd.dtbo` installed | Waveshare's mode A wiring. meta-raspberrypi installs neither overlay. |
| `godwit-can`, `can-utils` and `iproute2` in the image | Settings › CAN Buses, and the tools to watch a bus. BusyBox's `ip` cannot set CAN bit timing. |

The panel, backlight and touch drivers (`panel-waveshare-dsi-v2`,
`waveshare-panel-regulator`, `goodix-ts`) are already in the image as modules.
`cm5-godwit`'s `godwit-kms-config` picks the panel's DRM card because it is the
one with a connected output; with an HDMI display also plugged in it would
pick whichever it finds first.

## Troubleshooting

| Symptom | Fix |
|---|---|
| No backlight at all | Check the 5V and GND wires to the header, then the FFC (type, orientation, both latches closed). `journalctl -k \| grep -iE 'waveshare\|goodix'` shows `-121` when the panel is unpowered. |
| Backlight on, no picture | `ls /sys/class/drm/` should list `card*-DSI-2`. If it does not, check `config.txt` on the boot partition has the `dtoverlay` line and `overlays/vc4-kms-dsi-waveshare-panel-v2.dtbo` exists. Then `journalctl -u godwit-hmi -b`. |
| Picture upside down | Set `GODWIT_ROTATION=270` in the drop-in (see First boot). |
| A CAN port shows "Not found" | `journalctl -k \| grep -i mcp251` should show both controllers. Check the HAT is seated, its jumper is on 3.3V and `config.txt` has the `mcp251xfd` lines. |
| A CAN port is up but sees no traffic | Check the bit rates match the bus, CAN_H to CAN_H and CAN_L to CAN_L, and that the bus is terminated at both ends. `ip -details -statistics link show can0` shows the error counters. |
| Touch lands in the wrong place | The HMI maps touch through its rotation, so check `GODWIT_ROTATION` matches the picture, and that only one touch device is listed in `/dev/input/by-path`. |
