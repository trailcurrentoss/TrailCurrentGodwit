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
| `qml/DemoData.js` | The mockup's demo content (tracks, stations, contacts, the fixed map position) until each block is replaced by a backend, plus fixed tables such as `DIRS`, the maneuver icons and ring directions. |
| `qml/HeadUnit.qml` | The 1024×600 screen |
| `qml/Simulator.qml` | Desktop window: the screen plus both encoders, with gear, incoming-call, battery and theme controls |
| `qml/apps/` | One file per app |
| `src/knobs.*` | Encoder input (down / up / turn → press / hold / turn, hold at 550 ms), ring frames and display faces out |
| `src/dials.*`, `src/dialprotocol.h` | The USB encoder modules (hidraw): finds both dials, forwards their knob events to `Knobs`, sends them ring frames, faces and icons. Protocol: [dial-common/docs/protocol.md](../dial-common/docs/protocol.md) |
| `src/iconprovider.*` | Tints the bundled Ionicons SVGs (`image://icon/<name>/<color>`) |
| `src/config.h` | Map style and tile locations |
| `src/connman.*` | Wi-Fi through ConnMan over D-Bus (`Net` in QML): networks, power, scan, connect with a password agent, forget. |
| `src/bluez.*` | Bluetooth through BlueZ over D-Bus (`Bt` in QML): power, paired phones, pairing started from the phone with a code to confirm, connect, forget. |
| `src/ofono.*` | Phone calls through oFono over D-Bus (`Calls` in QML): the connected phone's calls; dial, answer, hang up, mute |
| `src/phonebook.*` | The connected phone's contacts and recent calls over Bluetooth PBAP through obexd (`Phonebook` in QML) |
| `src/camera.*` | `CameraList`: the USB cameras plugged in, by USB port. `CameraView`: live video from one of them through V4L2, streaming only while shown |
| `src/places.*` | Destination search (`Places` in QML): sends what is typed to Photon on the head unit (`127.0.0.1:2322`) and lists the matches nearest first, with distance and a category icon |
| `src/router.*` | Routing (`Router` in QML): Valhalla, linked in as a library and run on its own thread, plans the route to a chosen place. Gives the distance, time, the line for the map and the turn-by-turn steps |

Wi-Fi and Bluetooth use the real services on the head unit only (the eglfs
display). Elsewhere, Settings shows demo networks and phones so the simulator
leaves a PC's own radios alone. To test against real or mock services on a PC,
set `GODWIT_CONNMAN_BUS`, `GODWIT_BLUEZ_BUS`, `GODWIT_OFONO_BUS` or
`GODWIT_OBEX_BUS` to `system` or `session`. Each logs to the journal under
`godwit.net`, `godwit.bt`, `godwit.calls` and `godwit.phonebook`. The same
rule covers calls and contacts.

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

### Maps, search and routing

All three are offline and read a Headwaters map bundle installed on the
head unit; nothing in the HMI reaches the network for them. Installing a
bundle is covered in [../os/docs/maps.md](../os/docs/maps.md).

- **Map**: MapLibre reads the bundle's PMTiles directly from disk, with the
  Headwaters styles rewritten by `os/meta-godwit/recipes-godwit/godwit-map-style/files/godwit-style.py`.
  The route being previewed or driven is drawn on it as a line (`NavMap.qml`).
- **Search**: `Places` queries Photon, a local service on the head unit.
  Photon starts after the HMI, so for the first seconds after boot the search
  box says *Search is starting up*.
- **Routing**: `Router` runs Valhalla in-process. Choosing a search result
  plans the route for the preview; **Start** copies its steps into
  `Hmi.steps`, which the maneuver card, the ETA and the knob rings read.
  Valhalla is optional at build time: CMake finds it with pkg-config
  (`libvalhalla`), and without it, as in the simulator, routing reports
  unavailable.

Routes start from the fixed map position until a GPS source is connected,
and guidance does not yet advance as the vehicle moves.

On the head unit everything is installed in place. Elsewhere, point the HMI
at your own copies ([docs/simulator.md](docs/simulator.md#2-prepare-the-map-data-once)):

| Variable | Default |
|---|---|
| `GODWIT_MAP_STYLE_DIR` | `/usr/share/godwit/maps` (contains `styles/`, `fonts/`, `sprites/`) |
| `GODWIT_MAP_DATA` | `/data/maps/current/tiles.pmtiles` |
| `GODWIT_PHOTON_URL` | `http://127.0.0.1:2322` |
| `GODWIT_VALHALLA_CONFIG` | `/etc/valhalla/valhalla.json` |

Search and routing log to the journal under `godwit.places` and
`godwit.route`.

## Licences

Ionicons (MIT) in `assets/icons/` (`trending-up-back.svg` is `trending-up`
mirrored, for bearing left), Inter (OFL-1.1) in `assets/fonts/`.
The vehicle and water tank renders in `assets/vehicle/` and `assets/water/`
were generated for this project; each folder's README gives the prompts and
steps to make them again.
