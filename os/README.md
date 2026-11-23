# Building and flashing the Godwit image

The head unit runs Automotive Grade Linux **Vimba 22.0.0** (Yocto
*wrynose*). The same Godwit image builds for two boards:

| Board | Folder | Yocto machine | Boots from | Flashed with | Step-by-step guide |
|---|---|---|---|---|---|
| Radxa Dragon Q6A (Qualcomm QCS6490) | `boards/dragon-q6a` | `dragon-q6a` | NVMe, eMMC, microSD or USB | EDL over a USB-A cable, or `bmaptool` to a card | [boards/dragon-q6a/README.md](boards/dragon-q6a/README.md) |
| Raspberry Pi CM5 on a base board with an M.2 slot | `boards/rpi-cm5` | `raspberrypi5` | NVMe | `rpiboot` over the base board's USB-C port | [boards/rpi-cm5/README.md](boards/rpi-cm5/README.md) |
| Raspberry Pi 5 with an NVMe HAT and a Waveshare 10.1" DSI panel | `boards/rpi5-dsi` | `raspberrypi5` (build directory `build-rpi5-dsi`) | NVMe | `bmaptool` from the Pi booted off another card, or a USB NVMe enclosure | [boards/rpi5-dsi/README.md](boards/rpi5-dsi/README.md) |

**To build and flash a head unit, follow the guide for your board.** This
page covers what the boards share: preparing the build machine, how the
folders fit together, and the head unit layer.

## How the folders fit together

```
os/
  meta-godwit/          the head unit: image, HMI, maps, routing, speech, music. Shared by every board.
  boards/
    dragon-q6a/
      board.env         machine, layer and AGL features for this board
      meta-dragon-q6a/  board support: kernel, firmware, disk layout
        dynamic-layers/godwit/   this board's changes to the HMI recipe
      flash-edl.sh      writes the image over EDL
      flash/            staged image and EDL tools (not in git)
    rpi-cm5/
      board.env
      meta-rpi-cm5/     NVMe boot, on top of AGL's meta-raspberrypi
        dynamic-layers/godwit/
      flash-cm5.sh      writes the image over rpiboot
    rpi5-dsi/
      board.env         reuses rpi-cm5's NVMe and HMI features
      meta-rpi5-dsi/    the Waveshare 10.1" DSI panel, and the HMI turned to landscape
        dynamic-layers/godwit/
  scripts/setup.sh      fetches AGL and makes a build directory for one board
  docs/maps.md          installing the offline map bundle (every board)
  agl/                  the AGL checkout and every board's build directory (not in git)
```

Two rules keep the boards apart:

- **`meta-godwit` never names a board.** Anything that depends on the
  hardware lives in that board's `dynamic-layers/godwit/` folder. BitBake
  only reads it when meta-godwit is part of the build.
- **The HMI never knows which board it's on.** Each board's layer turns its
  hardware into the same fixed names: the GNSS receiver's I2C bus is always
  `/dev/gnss-i2c`, the display is always reached through eglfs on KMS, and
  the encoder dials are always USB HID devices. The dial firmware
  ([../dial-common](../dial-common)) is the same for every board.

## Prepare the build machine (once)

These steps are for Ubuntu 24.04. A first build needs about 150 to 200 GB
of free disk and takes 4 to 10 hours on a 16 to 20 thread machine. A build
that reuses another build's caches (below) is much faster.

Install the Yocto build dependencies, plus `libusb` and `pkg-config` for the
CM5's flashing tool:

```sh
sudo apt install build-essential chrpath cpio debianutils diffstat file \
    gawk gcc git iputils-ping libacl1 liblz4-tool locales python3 \
    python3-git python3-jinja2 python3-pexpect python3-pip python3-subunit \
    socat texinfo unzip wget xz-utils zstd curl bmap-tools \
    libusb-1.0-0-dev pkg-config
sudo locale-gen en_US.UTF-8
```

Ubuntu 24.04 blocks unprivileged user namespaces, and BitBake needs them to
cut network access from build tasks. Allow them for BitBake only:

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

Without this, the build stops early with an error about user namespaces.

## Create a build directory

Run this from this `os/` folder, naming your board:

```sh
scripts/setup.sh --board dragon-q6a      # or: --board rpi-cm5
```

The script fetches AGL into `agl/` and creates the board's build directory,
`agl/build-<machine>`. Every board shares the one AGL checkout, so setting
up a second board later only adds a build directory.

The first run asks a few questions. Each one is optional; leave it blank to
skip it:

| Question | Answer with | Leave blank to |
|---|---|---|
| Existing AGL checkout | another AGL build's `agl/` folder (the one holding `.repo`) | clone every layer (~1 GB) |
| Shared downloads | a `downloads/` folder to keep sources in | download ~30 GB of sources into this build |
| Shared sstate-cache | an `sstate-cache/` folder to keep built packages in | keep them in this build |
| Read-only sstate-cache | another build's `sstate-cache/`, which is read but never written | skip it |

Only reuse a checkout or cache from the same AGL release (Vimba 22.0.0).
Pointing every board at the same downloads and sstate-cache folders means
sources are fetched once and the build tools are built once.

The answers are saved to `scripts/dev.env`, which is not committed and
applies to every board. To change them, edit that file, or delete it to be
asked again. [scripts/dev.env.example](scripts/dev.env.example) explains
each setting.

When the script finishes it prints `Build directory ready` and the commands
for the next step. Running it again keeps an existing build directory's
settings. To regenerate them after changing `dev.env` or the features, run
`AGL_SETUP_FORCE=1 scripts/setup.sh --board <board>`. This overwrites that
build's `conf/local.conf` and `conf/site.conf`, so note any lines you added
there first.

To choose the AGL features yourself, list them after the board, for example
`scripts/setup.sh --board dragon-q6a agl-demo godwit` for an image without
development tools. Each board's defaults are in its `board.env`.

## Build the image

```sh
source agl/build-<name>/agl-init-build-env    # build-dragon-q6a, build-raspberrypi5 or build-rpi5-dsi
bitbake godwit-image
```

`source` moves you into the build directory. Run it again in every new
terminal before using `bitbake`. To switch boards, open a new terminal and
source the other board's build directory.

BitBake prints a line like `Sstate summary: ... 86% match` near the start,
which is how much it can reuse. If the build is interrupted or fails on a
network error, run the same `bitbake` command again; it picks up where it
stopped. BitBake stops when less than 1 GB of disk is free.

The image lands in `agl/build-<machine>/tmp/deploy/images/<machine>/` as
`godwit-image-<machine>.rootfs-<date>.wic.zst`. To save disk, each package's
working files are deleted once it's built (`INHERIT += "rm_work"` in the
build's `conf/site.conf`). Remove that line if you need them for debugging.

Then flash it as your board's guide describes. A full flash replaces the whole
drive: settings, Bluetooth pairings and any installed map bundle go with it. On a
board with separate partitions (Dragon Q6A), `flash-edl.sh --update` writes only
a new build and keeps them.

## Layer: `meta-godwit`

The head unit, on top of whichever board layer is in the build. Enabled by
the `godwit` AGL feature.

| Piece | What it does |
|---|---|
| `godwit-image` | `agl-image-minimal` plus the HMI. No Wayland compositor: the HMI draws straight to the display with Qt's `eglfs` on KMS, so it is the first thing on screen. |
| `godwit-hmi` | The Qt Quick HMI from [`../hmi`](../hmi), started by `godwit-hmi.service` with `DefaultDependencies=no` as soon as a display is connected. Nothing slow is ordered before it. Each board's `dynamic-layers/godwit/godwit-hmi.bbappend` adds the udev rule naming its GNSS bus `/dev/gnss-i2c`, and any display or audio setup the board needs. |
| `maplibre-native-qt` | Vector map renderer with a Qt Quick item. Reads PMTiles straight from disk, so no tile server runs. |
| `godwit-map-style` | The [Headwaters](https://github.com/trailcurrentoss/TrailCurrentHeadwaters) light map style and Godwit's night repaint of it (`night`), Noto Sans glyphs and sprites, rewritten by `godwit-style.py` to read local files. |
| `photon` | Destination search: the Photon 1.3.0 jar and `photon.service`, which serves the map bundle's search index on `127.0.0.1:2322` only. Starts after the HMI, once a bundle is installed. Runs as the `photon` user. |
| `temurin-jre-bin` | The Java 21 runtime Photon needs: Adoptium's prebuilt aarch64 JRE, without its X11 and sound libraries. The layers here only build OpenJDK 21 for the host. |
| `valhalla` | Routing and turn-by-turn maneuvers, built as a static library that `godwit-hmi` links, so no routing server runs. `valhalla-conf` installs `/etc/valhalla/valhalla.json`, generated at build time to read the bundle's `valhalla_tiles.tar`, with no tile URL. `valhalla-tools` has the command-line tools and is not in the image. |
| `piper-tts` | Spoken navigation prompts, offline: [Piper](https://github.com/rhasspy/piper)'s prebuilt aarch64 release (with its ONNX Runtime and espeak-ng) in `/usr/lib/piper`, and the LJSpeech English voice (public-domain recordings) in `/usr/share/piper/voices`. The HMI keeps one Piper running and plays its output with `pw-play` (`pipewire-tools`) on PipeWire's navigation role. |
| `whisper-cpp` | Offline speech to text for dictated message replies and the voice assistant. Built for `armv8.2-a+dotprod+fp16`, which every supported board's cores have; a board with older cores sets `WHISPER_ARM_ARCH`. |
| `godwit-radio` | The FM tuner and the aircraft and ship receivers from [`../radio`](../radio), with the udev rule giving the HMI the dongle and the blacklist keeping the kernel's DVB-T driver off it. The HMI recommends it; `rtl-sdr` comes from AGL's `meta-agl-demo` layer. |
| `godwit-aircraft-data` | The Aircraft app's tables of aircraft types and routes, about 10 MB, packed at build time from a pinned commit of Virtual Radar Server's Standing Data. The HMI recommends it. Move `SRCREV` to refresh them. |
| `godwit-wake` | The "Hey Peregrine" wake word detector from [`../wake`](../wake), with its model and openWakeWord's two feature models. The HMI recommends it (`godwit-hmi.bbappend`); each board's layer adds the language model where the board has an NPU. |
| `onnxruntime-bin` | ONNX Runtime's prebuilt aarch64 release, for `godwit-wake` |
| `pipewire`, `wireplumber-config-agl` (bbappends) | PipeWire is built with WebRTC's audio processing, which AGL leaves out. `62-godwit-mic.conf` loads the echo-cancel module to make `godwit-mic`: whichever microphone is the default, with noise suppression, automatic gain and what the head unit itself plays cancelled. The HMI's wake word and dictation listen to it (`GODWIT_MIC` in `godwit-hmi.service`). It is never the default source itself. `60-godwit-volume.conf` starts outputs at full volume, so the HMI's per-role volumes are the only controls. It starts microphones 15 dB below full: at WirePlumber's default of full, a webcam mic clips a nearby voice and the wake word stops hearing it. |
| GNSS receiver | Wired to header pins 1, 3, 5 and 6 on both boards (see the board's wiring diagram). The HMI reads `/dev/gnss-i2c`, and `i2c-tools` is in the image for checking it: `i2cdetect -y -r $(readlink /dev/gnss-i2c \| sed 's/i2c-//')` shows the receiver at `20`. |
| `godwit-growroot` | `godwit-growroot.service`: on first boot, grows the root partition to the end of its drive and its ext4 filesystem with it, online, beside the HMI. |

Navigation is fully offline: the map, search and routing all read a
Headwaters map bundle (`maps-<date>.zip`, built by Headwaters'
`build/maps/build.sh`) installed under `/data/maps`, the same layout
Headwaters uses. Nothing in it reaches the network. To install or update a
bundle, see **[docs/maps.md](docs/maps.md)**.

How the drive is laid out depends on the board's `GODWIT_STORAGE`:

- **`godwit-growroot`** (the default for a new board). The root partition is only
  as large as the image when flashed. `godwit-growroot.service` grows it and its
  filesystem to fill the drive on first boot, alongside the HMI. **This gives no
  factory reset:** nothing acts on the request from Settings, so a board that will
  hold anyone's data needs `godwit-storage`.
- **`godwit-storage`** (Dragon Q6A and Raspberry Pi CM5). `godwit-preinit` runs
  before systemd (`init=` on the kernel command line) and on first boot adds to the
  flashed boot and root:

  | Partition | Size | Holds | New build | Factory reset |
  |---|---|---|---|---|
  | `boot` | 128 MB | Kernel and bootloader | Replaced | Kept |
  | `root` | 40 GB | The system, read only | Replaced | Kept |
  | `godwit-data` | 100 GB, at `/data` | Settings, phones, Wi-Fi, media | Kept | Erased |
  | `godwit-content` | The rest, at `/data/maps` | Map bundles | Kept | Kept |

  `/etc` and `/var` are overlays held in memory, so anything written there is gone
  at power off, except the folders `godwit-preinit` keeps on the data partition
  (its `KEEP` list). A service that must remember something needs a folder there.
  The language and voice models are in the image, so a new build brings them.

  **Settings > System > Factory Reset** marks the data partition for erasing and
  restarts. Before anything else runs, `godwit-preinit` discards the whole
  partition, which then reads back as zeros, and formats it again.

  **Anything personal a new app or service keeps must be on `godwit-data`,** so
  the factory reset covers it with no list to update. Use the HMI's state folder
  (`/var/lib/godwit`), `/var/cache/godwit` or `/data/media`. A service with its own
  folder needs it added to `KEEP`. Never put anything personal on the root
  partition or in `/data/maps`: both survive a factory reset. If the app keeps a
  new kind of thing, add it to what the confirmation in Settings says is erased.

  To try a package on a bench board without flashing, use `godwit-deploy` in place
  of `rpm`: the root partition is read only, and an `rpm` run from a shell writes
  its database to memory.

System users and groups have fixed IDs, as AGL requires: `godwit` (1100),
`photon` (1101), the `fastrpc` group (1102) and `godwit-llm` (1103), in
`meta-godwit/files/godwit-passwd` and `godwit-group`. A new service user or
group needs an entry there, or its recipe is skipped.

To try the HMI on a PC without building an image, see
[../hmi/docs/simulator.md](../hmi/docs/simulator.md).

## Adding a board

1. Create `boards/<name>/` with a `board.env` (copy one of the existing
   ones) naming the Yocto machine, the board's layer and its default AGL
   features. If another board already builds for the same machine, also set
   `BUILD_NAME` so the two get separate build directories (`agl/build-<BUILD_NAME>`).
2. Put the board's layer in that folder. If AGL already supports the
   board's machine, as it does the Pi 5, the layer only holds what differs;
   otherwise it needs a machine template in `templates/machine/<machine>`.
3. Add `dynamic-layers/godwit/` to the layer for the board's changes to the
   HMI, enabled in its `conf/layer.conf` with
   `BBFILES_DYNAMIC += "godwit:${LAYERDIR}/dynamic-layers/godwit/recipes-*/*/*.bbappend"`.
   At minimum it needs a udev rule that names the GNSS receiver's I2C bus
   `/dev/gnss-i2c`.
4. Add a flash script and a `README.md` with the build and flash steps, and
   a row to the table at the top of this page.

`meta-godwit` and the HMI should need no changes. If they do, the change
probably belongs in the board's layer.

## Known gaps (every board)

- **Position**: until the GNSS receiver has a fix (it needs a view of the
  sky), the map, search and routes use a fixed position in Moab, UT.
- **Saved and recent places**: search has none yet; it shows only what you
  type.
- **Encoders**: the two rotary encoder modules connect over USB as HID
  devices; see [dial-common](../dial-common/). Everything they do can also be
  done by touch.
- **Phone audio**: a connected phone's contacts and recent calls show on the
  Phone screen (PBAP through `obexd`, which `godwit-obexd.service` runs on
  the system bus for the HMI), and its calls show and can be answered,
  dialled and ended (HFP through oFono).
- **CAN**: the TrailCurrent bus is read and commanded (lights, relays,
  battery, tanks, level, thermostat, gas and door alerts; see
  [hmi/README.md](../hmi/README.md#trailcurrent-bus)). The vehicle bus is not
  read yet, so climate and gear are still demo data.
- **Cameras**: USB (UVC) cameras are read directly through V4L2 at 640×480.
  They fill the Rear, Left, Right and Front views in USB port order, or as
  chosen in Settings > Cameras. That choice, like the other settings, is not
  yet kept across a restart.
- **Gear**: the HMI assumes Park until the vehicle CAN bus reports the real
  gear, so the password keyboards work on the bench.

Each board's guide lists its own gaps.
