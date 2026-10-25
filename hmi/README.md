# Godwit HMI

Qt 6 / Qt Quick head unit UI for the Godwit dual-encoder head unit. It is a
port of the design mockup `Dual Encoder Head Unit v2.dc.html` under `DOCS/`:
1024×600 touch screen, 13 apps, and two encoders, each with a round display
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
| `qml/Simulator.qml` | Desktop window: the screen plus both encoders, with gear, incoming-call, text-message, battery and theme controls |
| `qml/apps/` | One file per app |
| `src/knobs.*` | Encoder input (down / up / turn → press / hold / turn, hold at 550 ms), ring frames and display faces out |
| `src/dials.*`, `src/dialprotocol.h` | The USB encoder modules (hidraw): finds both dials, forwards their knob events to `Knobs`, sends them ring frames, faces and icons. Protocol: [dial-common/docs/protocol.md](../dial-common/docs/protocol.md) |
| `src/iconprovider.*` | Tints the bundled Ionicons SVGs (`image://icon/<name>/<color>`) |
| `src/config.h` | Map style and tile locations |
| `src/connman.*` | Wi-Fi through ConnMan over D-Bus (`Net` in QML): networks, power, scan, connect with a password agent, forget. |
| `src/bluez.*` | Bluetooth through BlueZ over D-Bus (`Bt` in QML): power, paired phones, pairing started from the phone with a code to confirm, connect, forget. |
| `src/ofono.*` | Phone calls through oFono over D-Bus (`Calls` in QML): the connected phone's calls; dial, answer, hang up, mute |
| `src/phonebook.*` | The connected phone's contacts and recent calls over Bluetooth PBAP through obexd (`Phonebook` in QML) |
| `src/messages.*` | New text messages on the connected phone over Bluetooth MAP through obexd (`Messages` in QML): reports each new inbox message with its sender, and downloads its text when asked |
| `src/camera.*` | `CameraList`: the USB cameras plugged in, by USB port. `CameraView`: live video from one of them through V4L2, streaming only while shown |
| `src/places.*` | Destination search (`Places` in QML): sends what is typed to Photon on the head unit (`127.0.0.1:2322`) and lists the matches nearest first, with distance and a category icon |
| `src/gnss.*` | The vehicle's position (`Gnss` in QML) from the DFRobot Gravity GNSS receiver on I2C (`/dev/gnss-i2c`, address 0x20), read once a second on its own thread with the Bearing firmware's register map |
| `src/clock.*` | The system clock and time zone (`Clock` in QML) through systemd-timedated: Settings' manual date and time, the time zone, and the time from the GNSS receiver |
| `src/dictation.*` | Speech to text for message replies (`Dictation` in QML): `pw-record` from PipeWire's default microphone, stopping after a pause, then whisper.cpp (`whisper-cli`, base.en) on the head unit |
| `src/voice.*` | Spoken navigation prompts (`Voice` in QML): one Piper process turns each prompt into a WAV, played with `pw-play` on PipeWire's navigation role; a new prompt cuts off one still playing |
| `src/library.*` | The media library for the Pictures and Videos apps (`Photos` and `Videos` in QML): lists `/data/media/photos` and `/data/media/videos`, and imports the files on a USB drive that are not in them yet. udev mounts drives read-only under `/run/godwit-usb` (`71-godwit-usb.rules`), so one can be pulled out at any time. `Videos` also takes a still and the length of each video once, kept in the cache folder. Videos play through Qt Multimedia's FFmpeg backend, in the page or full screen; both apps work in Park only. `GODWIT_PHOTOS_DIR`, `GODWIT_VIDEOS_DIR` and `GODWIT_USB_DIR` point the simulator at other folders; any folder in the USB folder that is not empty counts as a drive |
| `src/router.*` | Routing (`Router` in QML): Valhalla, linked in as a library and run on its own thread, plans the route to a chosen place. Gives the distance, time, the line for the map and the turn-by-turn steps |

Wi-Fi and Bluetooth use the real services on the head unit only (the eglfs
display). Elsewhere, Settings shows demo networks and phones so the simulator
leaves a PC's own radios alone. To test against real or mock services on a PC,
set `GODWIT_CONNMAN_BUS`, `GODWIT_BLUEZ_BUS`, `GODWIT_OFONO_BUS` or
`GODWIT_OBEX_BUS` to `system` or `session`. Each logs to the journal under
`godwit.net`, `godwit.bt`, `godwit.calls`, `godwit.phonebook` and
`godwit.messages`. The same rule covers calls, contacts and messages.

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

- **Position**: the map follows the vehicle from the GNSS receiver until it
  is dragged (Recenter resumes), and search ranking, routes and guidance all
  start from it. **Start** recentres on the vehicle and resumes following, at
  zoom 17 when the first maneuver is under half a mile away, 16 under 3
  miles, and 15 beyond. The map button above 2D/3D switches between north up
  (**N**) and direction of travel up, which turns the map to the receiver's
  course while moving faster than 3 mph. Guidance follows the vehicle along the route's line: the
  distance to the next maneuver counts down, steps advance as each is passed,
  prompts show at 0.5 and 0.1 mi, and three fixes more than 80 m off the line
  plan a new route from where the vehicle is. Without a fix (or in the
  simulator) everything uses a fixed position in Moab, Utah.

On the head unit everything is installed in place. Elsewhere, point the HMI
at your own copies ([docs/simulator.md](docs/simulator.md#2-prepare-the-map-data-once)):

| Variable | Default |
|---|---|
| `GODWIT_MAP_STYLE_DIR` | `/usr/share/godwit/maps` (contains `styles/`, `fonts/`, `sprites/`) |
| `GODWIT_MAP_DATA` | `/data/maps/current/tiles.pmtiles` |
| `GODWIT_PHOTON_URL` | `http://127.0.0.1:2322` |
| `GODWIT_VALHALLA_CONFIG` | `/etc/valhalla/valhalla.json` |
| `GODWIT_GNSS_DEVICE` | `/dev/gnss-i2c` |

- **Voice guidance**: with Voice Guidance on (Settings > Navigation, or hold
  the left knob while a prompt shows to turn it off), the route's start is
  spoken as it begins ("Drive east on Main Street. Then turn right onto
  Center Street."), each maneuver at 2 miles (when the leg is longer than 3),
  half a mile and as it is reached, and a new route or the arrival when they
  happen. The words are Valhalla's own spoken instructions. Prompts are
  silent during a call. Their loudness is the guidance volume (the
  navigation role): while a prompt is speaking, and for two seconds after,
  the left knob turns the guidance volume instead of the media volume, in
  any gear; press skips the prompt and hold turns voice guidance off; `GODWIT_VOICE=1` turns them on off the head unit, with
  `GODWIT_PIPER` and `GODWIT_PIPER_VOICE` pointing at Piper and a voice.

Search, routing, the receiver and the voice log to the journal under
`godwit.places`, `godwit.route`, `godwit.gnss` and `godwit.voice`.

### Clock

Settings > Units, Date & Time sets the system clock through
systemd-timedated, which also writes the board's battery-backed RTC. With
**Set Time Automatically** on, the clock follows the GNSS receiver's UTC time
(there is no network time: the head unit is offline). With it off, **Date**,
**Hour** and **Minute** rows step the clock by touch or the right knob. **Time
Zone** sets the system zone (Mountain is `America/Denver`). The `godwit` user
may do both by a polkit rule the HMI recipe installs. Like other system
services, this is used on the head unit only; set `GODWIT_TIMEDATE_BUS` to try
it elsewhere. It logs under `godwit.clock`.

### Text messages

A new text on the connected phone raises a banner with the sender's name (from
the phone's contacts when it is one) and announces "Message from …". **Read**
(the left knob's press) downloads the message and reads it aloud with the
navigation voice; **Dismiss** (the right knob's press) closes the banner. It
closes by itself after 12 seconds. The text is never shown on screen, and a
message is only marked read on the phone when Read downloads it. A message
arriving during a call waits until the call ends. Turning either knob while
the banner is up still does what it would without it.

When the message has been read, the banner offers **Reply** for 10 seconds.
Reply listens on the microphone (music is muted meanwhile) and stops when you
pause, or when you press **Done Talking**. whisper.cpp writes out what you said
on the head unit, the HMI reads it back, and **Send** texts it to the sender
through the phone; holding Send listens again, **Cancel** drops it. Dictation
records from PipeWire's default source; set `GODWIT_MIC` to a source's node
name to use another. It logs under `godwit.dictation`. In the simulator,
Reply pretends to hear a fixed sentence.

Messages come over Bluetooth MAP from the same obexd as the contacts. The HMI
keeps a MAP session open while the phone is connected; obexd registers it for
the phone's notifications, and the phone sends them to obexd's notification
server. An iPhone sends them only with **Show Notifications** turned on for
Godwit in Settings › Bluetooth › (i). Each phone's **Message Notifications**
switch in Settings › Bluetooth turns this off for that phone. In the
simulator, **Text Message** shows a demo message and Read shows its text in a
toast, as there is no voice there.

## Licences

Ionicons (MIT) in `assets/icons/` (`trending-up-back.svg` is `trending-up`
mirrored, for bearing left), Inter (OFL-1.1) in `assets/fonts/`.
The vehicle and water tank renders in `assets/vehicle/` and `assets/water/`
were generated for this project; each folder's README gives the prompts and
steps to make them again.
