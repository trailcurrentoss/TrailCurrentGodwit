# Third-Party Licenses and Attribution

TrailCurrent Godwit is MIT-licensed (see [LICENSE](LICENSE)). This file lists
the files in this repository that are **not** under that license, and the
third-party software and data the head unit image and the encoder dials
ship with, each under its own upstream license.

The head unit shows the same information on screen, under
**Settings › System › About & Legal**. The list it shows is
[`hmi/qml/Legal.js`](hmi/qml/Legal.js); the full license texts it shows are
in [`hmi/assets/licenses/`](hmi/assets/licenses/). When a dependency, asset
or data source is added, update this file and `Legal.js` together.

---

## Files in this repository not under the MIT license

| Path | License | Why |
|---|---|---|
| `os/boards/dragon-q6a/meta-dragon-q6a/recipes-kernel/linux/files/*.patch` | GPL-2.0-only | Changes to the Linux kernel |
| `os/meta-godwit/recipes-connectivity/bluez5/files/*.patch` | GPL-2.0-or-later | Change to BlueZ |
| `os/boards/dragon-q6a/meta-dragon-q6a/recipes-multimedia/alsa/files/0001-ucm2-codecs-wcd938x-Use-CLS_H_LOHIFI.patch` | BSD-3-Clause | Upstream alsa-ucm-conf change by Radxa, carried unchanged |
| `hmi/assets/fonts/Inter-*.ttf` | OFL-1.1 | Inter typeface, see `hmi/assets/fonts/LICENSE-inter.txt` |
| `hmi/assets/icons/*.svg` | MIT | Ionicons, copyright (c) 2015-present Ionic, see `hmi/assets/icons/LICENSE-ionicons` |
| `hmi/assets/licenses/*.txt` | Each file is the license it names | Verbatim license texts (SPDX), shown on the head unit |

The manufacturers' 3D models of the parts the enclosure fits around are not
in this repository: they come with no license, so there is no permission to
redistribute them. To check the enclosure against the real parts, download
each model from its manufacturer and save it under the name below. Git
ignores these paths.

| Part | Save as |
|---|---|
| Radxa Dragon Q6A board ([Radxa downloads](https://docs.radxa.com/en/dragon/q6a/download)) | `CAD/reference/radxa_dragon_q6a_3d_stp_v1.21.stp` |
| 1.28" rotary display module (the encoder dials) | `CAD/reference/00-irad-128.stp` |
| UC-595 7" display | `CAD/UC-595 7in Display Model.step` |

The van and water tank renders in `hmi/assets/vehicle/` and
`hmi/assets/water/` were generated with a local image model (see the README
in each folder) and are offered under this repository's MIT license.

---

## Head unit software

Installed on the head unit by the image in `os/`. Godwit's own process links
Qt and FFmpeg (dynamically), MapLibre Native Qt, Valhalla and Boost. Photon, Piper and
whisper.cpp run as separate programs.

| Component | Version | License | Copyright | Role |
|---|---|---|---|---|
| [Qt](https://www.qt.io) | 6.12 | LGPL-3.0-only | The Qt Company Ltd. and other contributors | UI toolkit, linked dynamically |
| [FFmpeg](https://ffmpeg.org) | 8.0 | LGPL-2.1-or-later | FFmpeg contributors | Decodes videos for the Videos app, through Qt Multimedia. Linked dynamically, built without its GPL parts. |
| [MapLibre Native Qt](https://github.com/maplibre/maplibre-native-qt) | 4.0 | BSD-2-Clause | MapLibre contributors; portions Mapbox, Inc. | Map rendering. Built with `MLN_QT_WITH_LOCATION=OFF`, which leaves out its (L)GPL part. |
| [Valhalla](https://github.com/valhalla/valhalla) | 3.9.1 | MIT | Valhalla contributors; Mapillary AB, Mapzen | Routing, linked into the HMI |
| [Boost](https://www.boost.org) | | BSL-1.0 | Boost contributors | C++ libraries |
| [Protocol Buffers](https://github.com/protocolbuffers/protobuf) | | BSD-3-Clause | Google Inc. | Valhalla's data format |
| [LZ4](https://github.com/lz4/lz4) | | BSD-2-Clause | Yann Collet | Compression |
| [zlib](https://zlib.net) | | Zlib | Jean-loup Gailly and Mark Adler | Compression |
| [ICU](https://icu.unicode.org) | | ICU | Unicode, Inc. and others | Text handling for map labels |
| [Photon](https://github.com/komoot/photon) | 1.3.0 | Apache-2.0 | komoot GmbH and contributors; includes OpenSearch | Destination search, separate service |
| [Eclipse Temurin JRE](https://adoptium.net) | 21 | GPL-2.0-only WITH Classpath-exception-2.0 | Oracle and/or its affiliates; Eclipse Adoptium contributors | Runs Photon |
| [Piper](https://github.com/rhasspy/piper) | 2023.11.14-2 | MIT | Michael Hansen | Text to speech, separate program |
| [eSpeak NG](https://github.com/espeak-ng/espeak-ng) | | GPL-3.0-or-later | eSpeak NG contributors | Phonemes for Piper, inside Piper's program |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | | MIT | Microsoft Corporation | Runs Piper's voice model |
| [Piper voice en_US-ljspeech-medium](https://huggingface.co/rhasspy/piper-voices) | | MIT; trained on the public-domain LJ Speech dataset | Michael Hansen; dataset Keith Ito | The spoken voice |
| [whisper.cpp and ggml](https://github.com/ggml-org/whisper.cpp) | 1.7.6 | MIT | The ggml authors | Speech to text, separate program |
| [Whisper base.en model](https://github.com/openai/whisper) | | MIT | OpenAI | Speech recognition model |
| [Inter](https://rsms.me/inter/) | | OFL-1.1 | The Inter Project Authors | Interface font |
| [Ionicons](https://ionic.io/ionicons) | | MIT | Ionic | Interface icons |

### Map style (from TrailCurrent Headwaters)

Installed by `godwit-map-style`. See Headwaters'
[THIRD_PARTY_LICENSES.md](https://github.com/trailcurrentoss/TrailCurrentHeadwaters/blob/main/THIRD_PARTY_LICENSES.md).

| Component | License | Copyright |
|---|---|---|
| OpenMapTiles style (derived) | BSD-3-Clause | OpenMapTiles contributors, with TrailCurrent changes |
| Noto Sans glyphs | OFL-1.1 | Google Inc. |
| Roboto glyphs | Apache-2.0 | Google Inc. |
| Metropolis glyphs | Unlicense | Chris Simpson |
| Maki icons 8.0.0 | CC0-1.0 | Mapbox and contributors |

### Operating system

The image is Automotive Grade Linux, built with the Yocto Project. It
includes the Linux kernel (GPL-2.0-only), the aic8800 Wi-Fi driver
(GPL-2.0-only and GPL-3.0-or-later), BlueZ, ConnMan and oFono
(GPL-2.0-or-later), PipeWire and WirePlumber (MIT), systemd
(LGPL-2.1-or-later), Mesa (MIT) and many other packages. The image installs
every package's license text, and a manifest of packages and licenses,
under `/usr/share/common-licenses` (`COPY_LIC_DIRS` and `COPY_LIC_MANIFEST`
in `godwit-image.bb`).

The Dragon Q6A firmware (`dragon-q6a-firmware`, from
[radxa-pkg/radxa-firmware](https://github.com/radxa-pkg/radxa-firmware))
is binary firmware from Radxa and Qualcomm, marked `CLOSED` in its recipe.
Check its redistribution terms before distributing a built image.

---

## Encoder dial firmware

Built from `dial-left/`, `dial-right/` and `dial-common/` and statically
linked into each dial's firmware image.

| Component | Version | License | Copyright |
|---|---|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) | 5.5 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. |
| esp_lvgl_port, esp_lcd_gc9a01, esp_tinyusb, cmake_utilities | 2.4 / 2.0 / 2.3 / 0.5 | Apache-2.0 | Espressif Systems (Shanghai) Co., Ltd. |
| [LVGL](https://github.com/lvgl/lvgl) | 9.2.2 | MIT | LVGL Kft |
| [TinyUSB](https://github.com/hathach/tinyusb) | 0.21 | MIT | Ha Thach (tinyusb.org) |

---

## Map, search and routing data

Map bundles are not in this repository. They are built with Headwaters' map
pipeline and installed on the head unit's drive (see
[os/docs/maps.md](os/docs/maps.md)).

- **OpenStreetMap data**, in the map tiles, the search index and the routing
  tiles: © OpenStreetMap contributors, under the
  [Open Database License 1.0](https://opendatacommons.org/licenses/odbl/).
  Anyone who passes on a bundle, or data taken from one, must keep the
  attribution and share that data under the ODbL.
- **OpenMapTiles schema**, the layout of the vector tiles: © OpenMapTiles,
  under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

The map screen shows "© OpenMapTiles © OpenStreetMap contributors" whenever
a map is drawn, as both licenses require.

---

## Distributing a built image or a device

Shipping a head unit image, or a device with one installed, brings
obligations beyond this repository's license:

- **GPL and LGPL source.** Provide the complete corresponding source for
  the GPL and LGPL packages in the image: the kernel, Qt, eSpeak NG, the
  Java runtime, BlueZ and the rest. Yocto's `archiver` class can collect it,
  and `create-spdx` produces an SBOM. Settings › About & Legal › Source Code
  carries the written offer.
- **Codec patents.** Videos are decoded by FFmpeg, whose recipe carries
  Yocto's `commercial` license flag: H.264, HEVC, AAC and other formats it
  decodes may need patent licenses where a device is sold. `meta-godwit`
  accepts the flag (`commercial_ffmpeg` in its `layer.conf`) so the image
  can play videos; settle the licensing before shipping a device.
- **Replacing Qt (LGPL-3.0).** The head unit is a consumer product under
  LGPLv3, so its owner must be able to install a modified Qt. Do not lock
  the bootloader or require signed images without providing the keys and
  instructions to do so.
- **Notices.** Keep the license texts in the image (`/usr/share/common-licenses`
  and the HMI's About & Legal pages) and the map attribution on screen.
