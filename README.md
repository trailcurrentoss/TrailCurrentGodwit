# The code here was written by AI, intended for demonstration purposes only DO NOT USE IN A VEHICLE!

# WIP: This repo is very much in early stage development
It is a reference application of Automotive Grade Linux applied to a RV, Campervan, Overland Rig use case.

# TrailCurrent Godwit

Dual-encoder infotainment head unit: a touchscreen and two rotary encoders,
each with a round display and an RGB LED ring, running Automotive Grade Linux
on a Radxa Dragon Q6A, a Raspberry Pi CM5 or a Raspberry Pi 5. Part of the
[TrailCurrent](https://trailcurrent.com) open-source vehicle platform.

The UI is a Qt 6 / Qt Quick app drawn straight to the display, with no
compositor in between, so the head unit boots fast. Navigation (map,
destination search and turn-by-turn routing) is fully offline, using the same
map bundles as
[TrailCurrent Headwaters](https://github.com/trailcurrentoss/TrailCurrentHeadwaters).
A "Hey Peregrine" voice assistant (directions, calls, texts, music and the
radio) also runs entirely on the head unit, its language model on the Dragon
Q6A's NPU. So do FM radio, weather radio and a scope of the aircraft and ships
nearby, from an RTL-SDR USB dongle.

## Quick Navigation

| I want to... | Go here |
|---|---|
| Try the UI on a PC, without building an image | [hmi/docs/simulator.md](hmi/docs/simulator.md) |
| Understand or change the UI code | [hmi/README.md](hmi/README.md) |
| Build and flash the image for the Radxa Dragon Q6A | [os/boards/dragon-q6a/README.md](os/boards/dragon-q6a/README.md) |
| Build and flash the image for the Raspberry Pi CM5 | [os/boards/rpi-cm5/README.md](os/boards/rpi-cm5/README.md) |
| Build and flash the image for a Raspberry Pi 5 with a Waveshare 10.1" DSI panel | [os/boards/rpi5-dsi/README.md](os/boards/rpi5-dsi/README.md) |
| Prepare a build machine, or see how the boards share one image | [os/README.md](os/README.md) |
| Build or install the offline maps, search and routing data | [os/docs/maps.md](os/docs/maps.md) |
| Build and flash the encoder dials | [dial-common/README.md](dial-common/README.md) |
| Use or change the voice assistant | [hmi/README.md#voice-assistant](hmi/README.md#voice-assistant), [wake/README.md](wake/README.md), [llm/README.md](llm/README.md) |
| Wire peripherals to the Dragon Q6A 40-pin header | [DOCS/boards/dragon-q6a/godwit-header-pinout.png](DOCS/boards/dragon-q6a/godwit-header-pinout.png) |

## Repository layout

| Path | What it holds |
|---|---|
| `hmi/` | The head unit UI (Qt 6 / QML and C++), plus its desktop simulator |
| `dial-left/`, `dial-right/` | ESP-IDF projects for the two encoder modules (one per side) |
| `dial-common/` | The firmware both dials share, and the USB protocol they speak with the HMI |
| `radio/` | What the RTL-SDR USB dongle receives: stereo FM with RDS, aircraft by ADS-B, ships by AIS and weather radio |
| `wake/` | The "Hey Peregrine" wake word detector, its model, and a tool for recording training samples on the head unit |
| `llm/` | The voice assistant's language model service on the Dragon Q6A's NPU |
| `os/` | The AGL build: `meta-godwit` (the head unit image, shared by every board) and `boards/` (one folder per board: its layer, flash script and guide) |
| `CAD/` | Enclosure design (FreeCAD) with STEP/STL exports. The manufacturers' models of the board, display and encoders are downloaded separately; see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). |
| `DOCS/` | The UI design mockups the HMI is built from, and each board's header wiring diagram in `DOCS/boards/` |

## License

MIT, see [LICENSE](LICENSE). Some files are under other licenses (patches to
upstream projects, fonts and icons), and
the head unit image and dials ship third-party software and OpenStreetMap
data under their own licenses. All of it is listed in
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md), and the head unit shows
it under Settings › System › About & Legal.
