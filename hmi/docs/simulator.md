# Running the HMI simulator

The simulator runs the Godwit head unit UI on a Linux PC: the 1024×600 screen
and both encoders, in one window. It is the same QML and C++ that runs on the
Dragon Q6A, so screens, knob behaviour, the map and both themes act as they
will on the device. Use it to try the UI without building or flashing an AGL
image. For how the HMI code is organised, see the [HMI README](../README.md).

## What you need

- Linux x86-64 with OpenGL (any desktop GPU driver, or Mesa's software
  renderer)
- Qt 6.8 or newer, desktop `gcc_64`, with Qt Quick and Qt SVG (both included
  in a standard Qt install)
- CMake 3.21 or newer and a C++20 compiler
- For the map: the ICU and OpenGL development headers, git, and about 2 GB of
  disk for the MapLibre source

On Ubuntu 24.04:

```sh
sudo apt install build-essential cmake git libgl-dev libicu-dev
```

Ubuntu's own Qt 6 packages are 6.4, which is too old. Install Qt 6.12 with
the [Qt Online Installer](https://www.qt.io/download-qt-installer) (choose
*Desktop gcc 64-bit*), or with [aqtinstall](https://github.com/miurahr/aqtinstall):

```sh
pipx install aqtinstall
aqt install-qt linux desktop 6.12.0 linux_gcc_64 -O ~/Qt
```

The commands below assume Qt is at `~/Qt/6.12.0/gcc_64`. Change `QT` if
yours is elsewhere. Run them from the top of the Godwit repository; build
output goes in `build/`, which git ignores.

```sh
QT=~/Qt/6.12.0/gcc_64
```

## 1. Build the map engine (once)

The map uses [MapLibre Native for Qt](https://github.com/maplibre/maplibre-native-qt),
at the same commit the head unit image builds. This takes about 10 minutes.
`CMAKE_INSTALL_RPATH` records where your Qt is, so the map plugin finds Qt's
libraries however the simulator is started.
Skip it if you don't need the map: the simulator then runs every screen and
shows "Map engine not available" on the map.

```sh
git clone https://github.com/maplibre/maplibre-native-qt build/maplibre-src
git -C build/maplibre-src checkout c3485f3a9590081c7be8edbc542341e688f6663d
git -C build/maplibre-src submodule update --init --recursive --depth 1
cmake -S build/maplibre-src -B build/maplibre-build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=$QT -DCMAKE_INSTALL_PREFIX=$PWD/build/maplibre \
    "-DCMAKE_INSTALL_RPATH=\$ORIGIN/../../lib;\$ORIGIN;$QT/lib" \
    -DMLN_WITH_OPENGL=ON -DMLN_QT_WITH_LOCATION=OFF -DMLN_QT_WITH_WIDGETS=OFF
cmake --build build/maplibre-build -j$(nproc)
cmake --install build/maplibre-build
```

## 2. Prepare the map data (once)

The map draws offline from a [Headwaters](https://github.com/trailcurrentoss/TrailCurrentHeadwaters)
map bundle, as the head unit does. You need:

- `tiles.pmtiles` from a Headwaters map bundle (`maps-<date>.zip`, built by
  Headwaters' `build/maps/build.sh`)
- a checkout of the Headwaters repository, for the map styles, fonts and
  sprites

`godwit-style.py`, the same script the image build uses, rewrites the
Headwaters styles to read these local files:

```sh
HW=../TrailCurrentHeadwaters              # your Headwaters checkout
TILES=/path/to/tiles.pmtiles               # from a Headwaters map bundle
WEB=$HW/containers/frontend/public/maps-static
STYLE=os/meta-godwit/recipes-godwit/godwit-map-style/files/godwit-style.py

mkdir -p build/maps/styles/3d build/maps/styles/3d-dark build/maps/fonts
ln -sfn "$(realpath $TILES)" build/maps/tiles.pmtiles
cp -r $WEB/sprites build/maps/
for f in "Noto Sans Regular" "Noto Sans Bold" "Noto Sans Italic"; do
    ln -sfn "$(realpath "$WEB/fonts/$f")" "build/maps/fonts/$f"
done
python3 $STYLE $WEB/styles/3d/style.json build/maps/styles/3d/style.json $PWD/build/maps $PWD/build/maps/tiles.pmtiles
python3 $STYLE $WEB/styles/3d-dark/style.json build/maps/styles/3d-dark/style.json $PWD/build/maps $PWD/build/maps/tiles.pmtiles --dark
```

The styles hold absolute paths, so run this again if you move the
repository. To use different tiles, point the `build/maps/tiles.pmtiles` link
at them.

## 3. Build the HMI

```sh
cmake -S hmi -B build/hmi -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$QT
cmake --build build/hmi -j$(nproc)
```

After changing the UI, run the second line again. Only changed files rebuild.

## 4. Run it

```sh
build/hmi/godwit-hmi --sim
```

A build in the repository's `build/` folder finds the map engine in
`build/maplibre` and the map data in `build/maps` by itself. To use other
copies, set these instead:

| Variable | What it points at |
|---|---|
| `QML_IMPORT_PATH` | The folder holding MapLibre's `MapLibre/` QML module |
| `GODWIT_MAP_STYLE_DIR` | Styles, fonts and sprites (step 2) |
| `GODWIT_MAP_DATA` | The tiles. The map page uses it to tell whether a map is installed. |

To run it from your desktop's app menu, add a `.desktop` entry under
`~/.local/share/applications/` that runs the command above. Keep it out of
this repository: it holds paths that only exist on your machine.

## Using the simulator

The window shows the head unit as in the design mockup: the screen, both
encoders with their round displays and LED rings, and a control bar above.

| Action | Mouse | Keyboard |
|---|---|---|
| Turn a knob | Drag round it, or scroll over it | Left `a` / `d`, right `j` / `l` |
| Press | Click | Left `s`, right `k` |
| Hold | Press and hold half a second | Shift + `s` / `k` |

Hold a knob down to see the ring sweep before the hold fires.

Changing a volume, by knob or by touch, shows its level at the top of the
screen for a moment: media or radio, navigation guidance, or phone.

The control bar along the top drives vehicle events:

- **P / R / D**: change gear. Reverse forces the rear camera and turns both
  rings into park distance displays.
- **Incoming Call**: a phone call rings in, taking over the knobs.
- **Low Battery**: raises the critical battery alert, which flashes both rings.
- **Dark / Light**: switch theme.

The screen also takes touch and mouse input directly: the dock, app tiles,
lists, toggles and the map (drag to pan, pinch to zoom).

Vehicle data (tanks, battery, route, music) is the mockup's demo data until
the head unit backends are connected.

Everything the knobs do can also be done by touch. Tap either knob's
indicator at the ends of the bottom bar for that knob's touch controls:
**−** / **+** to turn (hold to repeat) and buttons for its current press and
hold actions. Screens also have their own direct controls, such as dragging
the radio dial to tune, the temperature arrows on Climate and the zoom
buttons on the map.

On the map, **Where to?** opens destination search. Saved places and recent
destinations are listed straight away; typing needs Park. Pick a result for
the route preview, then **Start**. **Cancel**, at the far left on the driver's
side, leaves search at any point; in the route preview it reads **Back** and
returns to the results. The demo search covers places around Moab,
Utah, until the head unit's Photon search is connected.

In **Settings**, the right knob turns through a list, presses to open,
toggle or adjust the selected row, and holds to go back a page. Wi-Fi
networks, paired phones and CAN traffic are demo data in the simulator:

- **Wi-Fi → a locked network** opens the password keyboard. Typing is only
  allowed in Park (the simulator starts in Park). Any password of 8 or more
  characters connects. These are demo networks: the simulator talks to real
  Wi-Fi only where ConnMan runs, as on the head unit.
- **Bluetooth & Phones → Pair New Phone** shows a phone asking to pair after
  a few seconds; press the knob or **Pair** to accept the code.
- **CAN Buses → CAN 1 / CAN 2** sets which bus each port is wired to
  (vehicle or TrailCurrent), classic CAN or CAN FD, and the bit rates.
  Choosing a bus the other port already has swaps the two.

### Options

| Option | Effect |
|---|---|
| `--sim` | Simulator window with the encoders. Without it, the screen alone, as on the head unit. |
| `--app <id>` | Start on a screen: `nav`, `media`, `radio`, `climate`, `phone`, `energy`, `water`, `level`, `lights`, `cams`, `settings` |
| `--dark` | Start in the dark theme |
| `--grab <file.png>` | Save the window to an image after `--grab-after` milliseconds (default 2500), then quit |

## Troubleshooting

| Symptom | Cause |
|---|---|
| Map page says "Map engine not available" | MapLibre isn't built in `build/maplibre`, or it was built without `CMAKE_INSTALL_RPATH` (step 1). Running with `QML_IMPORT_TRACE=1` prints the exact reason. |
| Map page says "No map installed" | `GODWIT_MAP_DATA` doesn't name an existing `tiles.pmtiles` |
| Map area is blank or has no labels | The styles were made for a different tiles path or repository location; repeat step 2 |
| Window opens with nothing in it | No OpenGL. Check `glxinfo -B` (package `mesa-utils`) |
