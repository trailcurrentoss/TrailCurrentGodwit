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
scripts/setup.sh                          # repo sync + build dir (features: agl-demo agl-devel)
source agl/build-dragon-q6a/agl-init-build-env
bitbake agl-image-weston                  # bring-up image: Weston on HDMI
bitbake agl-ivi-demo-flutter              # full IVI demo
```

Images are written to
`agl/build-dragon-q6a/tmp/deploy/images/dragon-q6a/*.wic.zst`.

## Flash and boot

To write straight to onboard NVMe or eMMC over USB, use EDL mode:
[docs/flashing-edl.md](docs/flashing-edl.md) and `scripts/flash-edl.sh`.

For a microSD card or USB drive:

```sh
sudo bmaptool copy agl-image-weston-dragon-q6a.rootfs.wic.zst /dev/sdX
```

Insert the card and power on. The firmware boot order is USB, SD, NVMe,
eMMC, UFS. The serial console is `ttyMSM0` at 115200 on the 40-pin header.
The kernel console also appears on HDMI.

Use the 2026-01-20 SPI firmware release or newer. See the
[firmware thread](https://forum.radxa.com/t/radxa-dragon-q6a-firmware-snapshot/28886)
and [flashing guide](https://docs.radxa.com/en/dragon/q6a/low-level-dev/spi-fw).

## Known gaps

- **Wi-Fi/BT**: the onboard module is an AIC8800 on USB, which needs
  Radxa's out-of-tree [aic8800](https://github.com/radxa-pkg/aic8800) driver.
  It isn't packaged here yet. Ethernet and USB work.
- **Audio**: the 3.5 mm jack (WCD9385 codec) uses the upstream
  `QCS6490-Radxa-Dragon-Q6A` UCM profile. The layer backports two newer
  upstream fixes: the card-name link and the wcd938x headphone mode. USB audio
  devices work as-is. Not yet tested on hardware. If PipeWire comes up with
  only a dummy output, the codec probed after WirePlumber's first scan.
  Restarting WirePlumber fixes it, the same workaround the Ubuntu-based
  Playbill image uses.
- **Bring-up arguments**: `clk_ignore_unused pd_ignore_unused` are on the
  kernel command line. Remove them once every peripheral works.
