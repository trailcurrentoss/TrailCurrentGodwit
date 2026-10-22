# AGL on the Radxa Dragon Q6A

Automotive Grade Linux **Vimba 22.0.0** (Yocto *wrynose*) built for the
Radxa Dragon Q6A (Qualcomm QCS6490).

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

## Layer: `meta-godwit`

The Godwit head unit on top of the board layer, enabled by the `godwit` AGL
feature.

| Piece | What it does |
|---|---|
| `godwit-image` | `agl-image-minimal` plus the HMI. No Wayland compositor: the HMI draws straight to the display with Qt's `eglfs` on KMS, so it is the first thing on screen. |
| `godwit-hmi` | The Qt Quick HMI from [`../hmi`](../hmi), started by `godwit-hmi.service` with `DefaultDependencies=no` as soon as the display node exists. Nothing slow is ordered before it. |
| `maplibre-native-qt` | Vector map renderer with a Qt Quick item. Reads PMTiles straight from disk, so no tile server runs. |
| `godwit-map-style` | The [Headwaters](https://github.com/trailcurrentoss/TrailCurrentHeadwaters) light and dark map styles, Noto Sans glyphs and sprites, rewritten by `godwit-style.py` to read local files. |

Map tiles come from a Headwaters map bundle (`maps-<date>.zip`, built by
Headwaters' `build/maps/build.sh`). Unpack its `tiles.pmtiles` to
`/data/maps/versions/<date>/` and point the `/data/maps/current` symlink at
that directory, the same layout Headwaters uses.

To try the HMI on a PC without building an image, see
[../hmi/docs/simulator.md](../hmi/docs/simulator.md).

The boot entry has no menu delay and boots `quiet`. Press a key during boot to
get the systemd-boot menu.

## Host setup (once)

Ubuntu 24.04 blocks unprivileged user namespaces, and BitBake needs them to
cut network access from build tasks. Allow it for BitBake only:

```sh
sudo tee /etc/apparmor.d/bitbake >/dev/null <<'EOF'
abi <abi/4.0>,
include <tunables/global>
profile bitbake /**/bitbake/bin/bitbake flags=(unconfined) {
  userns,
}
EOF
sudo apparmor_parser -r /etc/apparmor.d/bitbake
```

A full build needs about 200 GB of disk.

## Build

Run these from this `os/` directory. The AGL checkout (`agl/`) and the
`repo` tool (`tools/`) are fetched here and are not tracked in git.

```sh
scripts/setup.sh                          # repo sync + build dir (features: agl-demo agl-devel godwit)
source agl/build-dragon-q6a/agl-init-build-env
bitbake godwit-image                      # Godwit head unit: Qt HMI on eglfs
bitbake agl-image-weston                  # bring-up image: Weston on HDMI
```

Images are written to
`agl/build-dragon-q6a/tmp/deploy/images/dragon-q6a/*.wic.zst`.

### Reusing an earlier build

If another Dragon Q6A AGL build exists on the same machine, point this one at
its downloads and shared state in `agl/build-dragon-q6a/conf/local.conf` to
skip most of the work (`OTHER` is that build's `build-dragon-q6a` folder):

```
DL_DIR = "OTHER/downloads"
SSTATE_MIRRORS = "file://.* file://OTHER/sstate-cache/PATH"
INHERIT += "rm_work"
```

Shared state only matches with the same hash-equivalence data, so before the
first build copy `OTHER/cache/hashserv.db` to `agl/build-dragon-q6a/cache/`.
`rm_work` deletes each recipe's work files once done, which keeps the build
well under the 200 GB above.

## Flash and boot

To write straight to onboard NVMe or eMMC over USB, use EDL mode:
[docs/flashing-edl.md](docs/flashing-edl.md) and `scripts/flash-edl.sh`.

For a microSD card or USB drive:

```sh
sudo bmaptool copy godwit-image-dragon-q6a.rootfs.wic.zst /dev/sdX
```

Insert the card and power on. The firmware boot order is USB, SD, NVMe,
eMMC, UFS. The serial console is `ttyMSM0` at 115200 on the 40-pin header.
The Godwit image boots quietly straight into the HMI; press a key during boot
for the systemd-boot menu.

Use the 2026-01-20 SPI firmware release or newer. See the
[firmware thread](https://forum.radxa.com/t/radxa-dragon-q6a-firmware-snapshot/28886)
and [flashing guide](https://docs.radxa.com/en/dragon/q6a/low-level-dev/spi-fw).

## Known gaps

- **Wi-Fi/BT**: the onboard AIC8800 (on USB) uses Radxa's out-of-tree
  [aic8800](https://github.com/radxa-pkg/aic8800) driver, packaged as
  `aic8800-usb` with Radxa's patch series and firmware. ConnMan manages Wi-Fi
  and Bluetooth power; the HMI's Settings screen drives Wi-Fi through ConnMan
  and phone pairing through BlueZ. Wi-Fi scans and connects on the board.
  BlueZ is configured to show the head unit to phones as "Godwit", a car
  audio device, and to run classic Bluetooth only (`ControllerMode = bredr`).
  In dual mode an iPhone stalls on the Low Energy key exchange the kernel
  starts after pairing, and reports the pairing as failed. If something does not work, check `lsmod | grep aic`,
  `dmesg | grep -i aic`, `connmanctl technologies`, `bluetoothctl show` and
  `journalctl -u godwit-hmi -b`.
- **Audio**: the 3.5 mm jack (WCD9385 codec) uses the upstream
  `QCS6490-Radxa-Dragon-Q6A` UCM profile. The layer backports two newer
  upstream fixes: the card-name link and the wcd938x headphone mode. USB audio
  devices work as-is. Not yet tested on hardware. If PipeWire comes up with
  only a dummy output, the codec probed after WirePlumber's first scan.
  Restarting WirePlumber fixes it, the same workaround the Ubuntu-based
  Playbill image uses.
- **Bring-up arguments**: `clk_ignore_unused pd_ignore_unused` are on the
  kernel command line. Remove them once every peripheral works.
- **HMI on hardware**: `godwit-hmi` has run on a desktop (the simulator) but
  not yet on the board. It runs as the unprivileged `godwit` user and takes the
  display directly; check `journalctl -u godwit-hmi -b` on first boot.
- **Map storage**: the root partition is only as large as the image, so a
  full map bundle does not fit. A `/data` partition that grows to fill the
  drive on first boot is still to be added to the disk layout.
- **Search and routing**: Photon and Valhalla from the map bundle are not
  packaged yet; destination search uses demo places.
- **Encoders**: the two rotary encoder modules connect over USB as HID
  devices; see [dial-common](../dial-common/). Everything they do can also be
  done by touch.
- **Phone audio**: a connected phone's contacts and recent calls show on the
  Phone screen (PBAP through `obexd`, which `godwit-obexd.service` runs on
  the system bus for the HMI), and its calls show and can be answered, dialled
  and ended (HFP through oFono). The image has no PipeWire yet, so call audio
  stays on the phone and music (A2DP) does not play through the head unit.
- **CAN**: the CAN settings and vehicle data are demo data until they are
  connected to SocketCAN.
- **Cameras**: USB (UVC) cameras are read directly through V4L2 at 640×480.
  They fill the Rear, Left, Right and Front views in USB port order, or as
  chosen in Settings > Cameras. That choice, like the other settings, is not
  yet kept across a restart.
- **Gear**: the HMI assumes Park until the vehicle CAN bus reports the real
  gear, so the password keyboards work on the bench.
