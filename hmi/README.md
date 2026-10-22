# Godwit HMI

Qt 6 / Qt Quick head unit UI for the Godwit dual-encoder head unit. It is a
port of the design mockup `Dual Encoder Head Unit v2.dc.html` under `DOCS/`:
1024×600 touch screen, 11 apps, and two encoders, each with a round display
and a ring of 5 RGB LEDs.

On the head unit it runs full screen with `eglfs` on KMS, so no Wayland
compositor sits between power-on and the first frame. The layout is
1024×600; on a display that runs at a higher resolution (for example a bench
HDMI display such as the UCTRONICS UC-565) it scales up to fill the screen,
keeping its proportions.

## Layout

| Path | What it is |
|---|---|
| `qml/Hmi.qml` | State, knob arbitration (`owner()`), knob handlers (`o_*`, `a_*`), encoder displays and ring patterns (`encs`). Function names follow the mockup's so the two can be compared side by side. |
| `qml/Ring.js` | LED colour maths and ring patterns |
| `qml/DemoData.js` | The mockup's demo content (tracks, stations, route, contacts). Each block is replaced by a backend. |
| `qml/HeadUnit.qml` | The 1024×600 screen |
| `qml/Simulator.qml` | Desktop window: the screen plus both encoders, with gear, incoming-call, battery and theme controls |
| `qml/apps/` | One file per app |
| `src/knobs.*` | Encoder input (down / up / turn → press / hold / turn, hold at 550 ms), ring frames and display faces out |
| `src/dials.*`, `src/dialprotocol.h` | The USB encoder modules (hidraw): finds both dials, forwards their knob events to `Knobs`, sends them ring frames, faces and icons. Protocol: [dial-common/docs/protocol.md](../dial-common/docs/protocol.md) |
| `src/iconprovider.*` | Tints the bundled Ionicons SVGs (`image://icon/<name>/<color>`) |
| `src/config.h` | Map style and tile locations |
| `src/connman.*` | Wi-Fi through ConnMan over D-Bus (`Net` in QML): networks, power, scan, connect with a password agent, forget. Where ConnMan isn't running, Settings shows demo networks. |

## Try it on a PC

The simulator runs this UI on a Linux PC, with the screen and both encoders in
one window, so you can try every screen and knob without building or flashing
an image. Setup, controls and options are in
**[docs/simulator.md](docs/simulator.md)**. In short:

```sh
cmake -S hmi -B build/hmi -DCMAKE_PREFIX_PATH=<qt6 prefix>
cmake --build build/hmi
build/hmi/godwit-hmi --sim
```

Every screen runs straight away. The map also needs the map engine and map
data prepared once, as the guide describes; after that the same command shows
it.

### Maps

The map reads a Headwaters map bundle's PMTiles directly from disk, with the
Headwaters styles rewritten by `os/meta-godwit/recipes-godwit/godwit-map-style/files/godwit-style.py`.
On the head unit these are installed in place. Elsewhere, point the HMI at
your own copies ([docs/simulator.md](docs/simulator.md#2-prepare-the-map-data-once)):

| Variable | Default |
|---|---|
| `GODWIT_MAP_STYLE_DIR` | `/usr/share/godwit/maps` (contains `styles/`, `fonts/`, `sprites/`) |
| `GODWIT_MAP_DATA` | `/data/maps/current/tiles.pmtiles` |

## Licences

Ionicons (MIT) in `assets/icons/`, Inter (OFL-1.1) in `assets/fonts/`.
