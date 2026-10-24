# Flashing the Dragon Q6A over EDL

EDL (Qualcomm Emergency Download) mode lets a host computer write straight to
the board's onboard storage over USB. Nothing needs to boot on the board, and
no SD card or USB drive is involved. Use it to put the Godwit image on NVMe
or eMMC.

Run every command on this page from the repository's `os/` folder, after
building the image as described in [README.md](README.md).

## What you need

- The Q6A with the target storage fitted: an M.2 2230 NVMe SSD, or an eMMC module
- Its 12 V USB PD Type-C power adapter
- A **USB-A to USB-A** data cable. EDL uses the board's USB 3.1 Type-A port,
  not the Type-C power port.
- This computer (Linux x86-64) with sudo
- An HDMI display, and Ethernet if you want it, for the first boot
- SPI firmware 2026-01-20 or newer on the board (see "If the board doesn't boot")

## Staged files

Everything lives in `boards/dragon-q6a/flash/`, which git ignores:

| File | What it is |
|---|---|
| `godwit-image-dragon-q6a.wic` | The Godwit image, decompressed (EDL writes raw sectors) |
| `godwit-image-dragon-q6a.wic.bmap` | Block map, for writing to SD or USB with `bmaptool` instead |
| `firmware/prog_firehose_ddr.elf` | Radxa's Firehose loader. EDL uploads it to the board, and it does the actual writing. |
| `firmware/dragon-q6a_flat_build_wp_260120.zip` | The full 2026-01-20 SPI firmware release the loader came from |
| `tools/edl-ng/linux-x64/edl-ng` | The EDL client ([strongtz/edl-ng](https://github.com/strongtz/edl-ng), MIT) |
| `SHA256SUMS` | Checksums of the files above |

Check that nothing has changed since staging:

```sh
(cd boards/dragon-q6a/flash && sha256sum -c SHA256SUMS)
```

## Step 1: Put the board in EDL mode

1. Remove power from the board. Take out any microSD card or USB drive, since
   the firmware boots those before NVMe.
2. Connect the board's **USB 3.1 Type-A** port to this computer with the A-to-A cable.
3. Press and **hold the EDL button**.
4. Connect the 12 V Type-C power adapter.
5. Once the board has power, **release** the button.
6. Check that the computer sees it:

   ```sh
   lsusb | grep 9008
   ```

   You should see `ID 05c6:9008 Qualcomm, Inc. ... (QDL mode)` or similar. If
   it isn't there, remove power and try again. The button must be held before
   power is applied.

## Step 2: Check the target (read-only)

Read the partition table of the storage you're about to overwrite. This confirms
that `edl-ng`, the loader and the drive all work before anything is written.

```sh
sudo boards/dragon-q6a/flash-edl.sh --target nvme --check     # or: --target emmc
```

`edl-ng` prints the drive's current GPT, along with the storage's sector size
in its log. Look for two things:

- **The partition list.** Whatever it shows is about to be erased. If it's an
  OS you want to keep, stop here and back it up first.
- **The sector size must be 512.** This image is built for 512-byte sectors. A
  4096-byte drive (some enterprise NVMe, and all UFS) needs a different image;
  if you write this one, the board drops to a `UEFI Shell>` prompt.

The board stays in EDL mode after the check, so you can go straight on.

## Step 3: Write the image

```sh
sudo boards/dragon-q6a/flash-edl.sh --target nvme             # or: --target emmc
```

The script shows the image and target, then asks you to type the target name
to confirm. Writing the image takes a few minutes.

To write a different image, such as AGL's Weston bring-up image, pass
`--image boards/dragon-q6a/flash/agl-image-weston-dragon-q6a.wic`.

## Step 4: First boot

1. Remove power and unplug the A-to-A cable.
2. Connect HDMI, and Ethernet if you want it.
3. Apply power **without** holding the EDL button.

Expect the Radxa logo, then the Godwit head unit screen on HDMI. There is no
boot menu and the kernel boots quietly; press any key on the serial console or
a USB keyboard during boot to get the systemd-boot menu. The serial console is
`ttyMSM0` at 115200 baud on header pins 8 (TX) and 10 (RX).

The map page says "No map installed" until a map bundle is copied to
`/data/maps` (see [maps.md](../../docs/maps.md)).

Log in on the serial console or over SSH and collect:

```sh
systemd-analyze                      # time to reach userspace
systemctl status godwit-hmi          # the HMI is running
journalctl -u godwit-hmi -b          # the HMI's log, including any QML errors
dmesg | grep -iE 'msm|adreno|zap'    # GPU came up and loaded its zap shader
cat /sys/class/drm/card*-*/status    # which connector is HDMI, and whether it's connected
aplay -l                             # the Q6A sound card is listed
journalctl -b -p err                 # anything that failed during boot
```

The kernel may warn that the backup GPT isn't at the end of the disk. That's
expected: the image is smaller than the drive. It's harmless.

## If the board doesn't boot

| Symptom | Likely cause | What to do |
|---|---|---|
| `UEFI Shell>` prompt | Sector-size mismatch, or the firmware didn't find the ESP | Check Step 2's sector size. Type `exit` and choose the NVMe or eMMC entry from the boot manager. |
| Stops at the systemd-boot menu | A key press reached the boot loader, possibly electrical noise on the debug UART's RX pin (seen on Playbill with a HAT fitted) | Remove any HAT and power-cycle |
| Radxa logo, then nothing | SPI firmware older than 2026-01-20 | Flash the SPI firmware (below), then boot again |
| Kernel starts, then a black screen | Display or GPU bring-up, or the HMI failed to start | Use the serial console or SSH and collect the commands from Step 4, starting with `journalctl -u godwit-hmi -b` |

### Updating the SPI firmware

You only need this once per board, and only if it has firmware older than
2026-01-20. Put the board in EDL mode (Step 1), then:

```sh
cd boards/dragon-q6a
mkdir -p flash/spinor
unzip -o flash/firmware/dragon-q6a_flat_build_wp_260120.zip 'flat_build/spinor/dragon-q6a/*' -d flash/spinor
cd flash/spinor/flat_build/spinor/dragon-q6a
sudo ../../../../tools/edl-ng/linux-x64/edl-ng --memory SPINOR \
    --loader prog_firehose_ddr.elf rawprogram rawprogram0.xml patch0.xml
```

This writes the board's boot firmware. Don't remove power while it runs.

## Re-staging after a rebuild

`flash-edl.sh` writes the image staged in `boards/dragon-q6a/flash/`, not
the one in the build's deploy folder. After each new `bitbake godwit-image`,
stage it again:

```sh
D=agl/build-dragon-q6a/tmp/deploy/images/dragon-q6a
S=boards/dragon-q6a/flash
# The deploy names are symlinks, which zstd skips; give it the file itself
zstd -df "$(readlink -f "$D/godwit-image-dragon-q6a.rootfs.wic.zst")" -o $S/godwit-image-dragon-q6a.wic
cp -L "$D/godwit-image-dragon-q6a.rootfs.wic.bmap" $S/godwit-image-dragon-q6a.wic.bmap
(cd $S && sha256sum godwit-image-dragon-q6a.wic firmware/* tools/edl-ng/linux-x64/edl-ng > SHA256SUMS)
```

On a fresh clone, `boards/dragon-q6a/flash/` doesn't exist yet. Get `edl-ng` from
[strongtz/edl-ng](https://github.com/strongtz/edl-ng) or Radxa's
[download page](https://docs.radxa.com/en/dragon/q6a/download), and the SPI
firmware zip from the
[Radxa firmware thread](https://forum.radxa.com/t/radxa-dragon-q6a-firmware-snapshot/28886/20).
Then lay them out as in the table above. If another checkout already has a
staged flash folder, its `firmware/` and `tools/` can be copied across
as they are. The script reads other locations from
`--image`, `--loader` and `--edl`, or from `FLASH_IMAGE`, `FLASH_LOADER` and
`FLASH_EDL`.

## References

- Radxa: [Entering EDL mode](https://docs.radxa.com/en/dragon/q6a/low-level-dev/edl-mode),
  [Using the EDL tool](https://docs.radxa.com/en/dragon/q6a/getting-started/install-system/usb-system/set-edl-variable),
  [Flashing SPI boot firmware](https://docs.radxa.com/en/dragon/q6a/low-level-dev/spi-fw)
