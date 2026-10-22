# TrailCurrent Godwit

Dual-encoder infotainment head unit: a touchscreen and two rotary encoders,
each with a round display and an RGB LED ring, running Automotive Grade Linux
on a Radxa Dragon Q6A. Part of the [TrailCurrent](https://trailcurrent.com)
open-source vehicle platform.

The UI is a Qt 6 / Qt Quick app drawn straight to the display, with no
compositor in between, so the head unit boots fast. Maps are fully offline,
using the same map bundles as
[TrailCurrent Headwaters](https://github.com/trailcurrentoss/TrailCurrentHeadwaters).

## Quick Navigation

| I want to... | Go here |
|---|---|
| Try the UI on a PC, without building an image | [hmi/docs/simulator.md](hmi/docs/simulator.md) |
| Understand or change the UI code | [hmi/README.md](hmi/README.md) |
| Build the AGL image for the Dragon Q6A | [os/README.md](os/README.md) |
| Flash the image to onboard NVMe or eMMC | [os/docs/flashing-edl.md](os/docs/flashing-edl.md) |
| Build and flash the encoder dials | [dial-common/README.md](dial-common/README.md) |

## Repository layout

| Path | What it holds |
|---|---|
| `hmi/` | The head unit UI (Qt 6 / QML and C++), plus its desktop simulator |
| `dial-left/`, `dial-right/` | ESP-IDF projects for the two encoder modules (one per side) |
| `dial-common/` | The firmware both dials share, and the USB protocol they speak with the HMI |
| `os/` | The AGL build: `meta-dragon-q6a` (board support) and `meta-godwit` (the head unit image) |
| `CAD/` | Enclosure design (FreeCAD) with STEP/STL exports and reference models of the board, display and encoders |
| `DOCS/` | The UI design mockups the HMI is built from |
