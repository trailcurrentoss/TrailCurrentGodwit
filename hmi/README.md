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
| `src/dictation.*` | Speech to text for message replies (`Dictation` in QML): `pw-record` from the microphone `GODWIT_MIC` names (`godwit-mic` on the head unit), stopping after a pause, then whisper.cpp (`whisper-cli`, base.en) on the head unit |
| `src/voice.*` | Spoken navigation prompts (`Voice` in QML): one Piper process turns each prompt into a WAV, played with `pw-play` on PipeWire's navigation role; a new prompt cuts off one still playing |
| `src/library.*` | The media library for the Pictures and Videos apps (`Photos` and `Videos` in QML): lists `/data/media/photos` and `/data/media/videos`, and imports the files on a USB drive that are not in them yet. udev mounts drives read-only under `/run/godwit-usb` (`71-godwit-usb.rules`), so one can be pulled out at any time. `Videos` also takes a still and the length of each video once, kept in the cache folder. Videos play through Qt Multimedia's FFmpeg backend, in the page or full screen; both apps work in Park only. `GODWIT_PHOTOS_DIR`, `GODWIT_VIDEOS_DIR` and `GODWIT_USB_DIR` point the simulator at other folders; any folder in the USB folder that is not empty counts as a drive |
| `src/trails.*` | GPX tracks for the map (`Trails` in QML), in `/data/media/trails`: imported from USB drives through `src/library.*`, or recorded from the receiver with the record button on the map. Each track has a color and can be shown or hidden; what was chosen is kept in `.trails.json` in that folder. A recording is written as it goes and only the record button ends it: if the power goes first, as when the engine is switched off for fuel, the next start carries on in the same file, with a new `<trkseg>` so no line is drawn across the stop. Showing and hiding work while driving; importing, naming, colors and deleting wait for Park. `GODWIT_TRAILS_DIR` points the simulator at another folder, where the record button draws a made-up route as there is no receiver |
| `src/radio.*` | The FM tuner (`Radio` in QML): runs [../radio](../radio) and `pw-play` while the radio is what is heard. Without it, in the simulator, the Radio app shows demo stations |
| `src/tunerreceiver.*` | What the Aircraft and Ships apps share: runs a decoder from [../radio](../radio) while the app is open. There is one dongle, so the radio is silent meanwhile, and each user of it waits for the others' `tunerHeld` to go false before opening it. Without a decoder, in the simulator, the apps show demo traffic |
| `src/aircraft.*` | Aircraft nearby (`Aircraft` in QML): `godwit-adsb`, with each aircraft's type and route looked up in the tables on the head unit |
| `src/ships.*` | Ships nearby (`Ships` in QML): `godwit-ais`. A ship sends its own name, type and destination |
| `src/weatherradio.*` | NOAA Weather Radio (`WeatherRadio` in QML): runs `godwit-wxradio` and `pw-play` while its app is open, and names the nearest transmitter on each channel from the table on the head unit |
| `qml/components/TrafficPage.qml` | The scope, detail card and Park's full list that the Aircraft and Ships apps share; `apps/AircraftApp.qml` and `apps/ShipsApp.qml` say only what differs |
| `src/music.*` | The Media app's sources (`Music` in QML): the head unit's own music in `/data/media/music` (Local) and the music on a USB drive, played through Qt Multimedia. See [Music sources](#music-sources) |
| `src/wake.*` | The wake word (`Wake` in QML): `pw-record` from the same microphone into `godwit-wake` ([../wake](../wake)), which listens for "Hey Peregrine" |
| `src/llm.*` | The language model (`Llm` in QML): sends a spoken request the patterns miss to the NPU model service on the head unit (`godwit-llm`, [../llm](../llm), `127.0.0.1:11434`) and returns the command it wrote |
| `qml/Intents.js` | The voice assistant's commands (directions, calls, texts, the radio, music and ending the route): the patterns that understand them, contact matching, and the check on what the language model wrote |
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

The logs normally say what happened, without what anyone said, their messages or
which phone. **Settings > System > Verbose Logging** adds those and more detail,
as `godwit.*` debug messages, for troubleshooting. It stays on through restarts
until it is turned off, which clears the journal, for every service. The journal
is kept in memory, so it is also gone at power off.

The HMI's user cannot clear the journal or restart the head unit itself, and the
image has no polkit. It leaves a file in its runtime directory instead, and
`godwit-clear-logs.path` and `godwit-restart.path` act on it as root.

### Music sources

The Media app plays from three sources, chosen with the buttons along the
bottom of the screen or by holding the right knob. One plays at a time:
choosing another, tuning the radio or a phone starting its music pauses the
rest. A call pauses the music and resumes it when the call ends. The media
volume starts at 10% every time the HMI starts, so the cabin is never loud at
power-on.

| Source | Plays | Needs |
|---|---|---|
| **Local** | Music imported into `/data/media/music`. Browse by All Songs, album or artist. | Nothing. **Import from USB** copies a drive's music in. |
| **USB** | The music on a plugged-in drive, straight from the drive | A drive (mounted read-only under `/run/godwit-usb`) |
| **Bluetooth** | The connected phone's music (A2DP and AVRCP) | A paired phone |

**Browse** on the right card holds a source's albums and artists; the right
knob turns through them and presses to play, in any gear. On **Up Next**, the
row the knob is on follows the playing song when a song ends or is skipped,
unless the driver has turned to another row.
For voice, `Hmi.playMusic(words, source, shuffle)` plays the best match from a source
(the one heard now, or Local): an artist or album by name, or else the songs whose
titles match.

| Variable | Default |
|---|---|
| `GODWIT_MUSIC_DIR` | `/data/media/music` |
| `STATE_DIRECTORY` | Where settings are kept; set by systemd on the head unit, `~/.local/share/godwit-hmi` elsewhere |

They log under `godwit.music`.

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

When the message has been read, the voice asks whether you want to reply and
listens for yes or no. The banner also offers **Reply**, and waits 10 seconds for
a press if no answer is heard.
Reply asks for your message and plays a tone, then listens on the microphone
(music is muted meanwhile) and stops when you
pause, or when you press **Done Talking**. whisper.cpp writes out what you said
on the head unit, the HMI reads it back, and **Send** texts it to the sender
through the phone; holding Send listens again, **Cancel** drops it. On the head
unit, dictation records from `godwit-mic`, the default microphone through
WebRTC's processing ([Layer: `meta-godwit`](../os/README.md#layer-meta-godwit)).
`godwit-hmi.service` sets `GODWIT_MIC` to it; set that to another source's node
name to use another, or leave it out for PipeWire's default source. It logs
under `godwit.dictation`. In the simulator,
Reply pretends to hear a fixed sentence.

Messages come over Bluetooth MAP from the same obexd as the contacts. The HMI
keeps a MAP session open while the phone is connected; obexd registers it for
the phone's notifications, and the phone sends them to obexd's notification
server. An iPhone sends them only with **Show Notifications** turned on for
Godwit in Settings › Bluetooth › (i). Each phone's **Message Notifications**
switch in Settings › Bluetooth turns this off for that phone. In the
simulator, **Text Message** shows a demo message and Read shows its text in a
toast, as there is no voice there.

### Voice assistant

Say **"Hey Peregrine"**, wait for the chime, then ask for one of these:

| Say | Does |
|---|---|
| "Get directions to Arches National Park", "Take me to the nearest gas station", "Directions to Maya's house" | Searches for the place, or for a contact's address when the place is a contact with one on the phone, and opens the route preview of the first match, saying how far and how long. **Press** the right knob to start, as with a place picked by hand. |
| "Call Maya", "Call 435 555 0142", "Dial four three five…" | Calls the contact (the closest name in the phone's contacts or recent calls) or the number |
| "Send a text to Jordan", "Text Maya I'm running late", "Send a message to 801 555 1212 saying on my way" | Asks for the message unless it was said, reads it back, and **Send** texts it, as a reply to a message does |
| "Set the radio to 101.9", "Radio preset 3" | Tunes the radio. AM is answered with a note that the tuner only receives FM |
| "Play Rumours from my library", "Put on some Johnny Cash", "Play Rumours from my library" | Plays it on the named source, or the one playing, or the first that is set up (`Hmi.playMusic`) |
| "Play my library", "Shuffle my music", "Play Tom Petty on shuffle", "Play some music" | "My library", "my music" or "all my songs" plays the whole Local library; "shuffle", "shuffled" or "on shuffle" turns shuffle on first (saying nothing leaves it as it was); "some music" plays the source chosen now |
| "Cancel the route", "Stop navigation" | Asks first: **End Route** (left knob) ends it, **Keep Route** (right knob), "no" or a pause keeps it. "Yes" ends it too. |
| "Mute the music", "Silence", "Unmute", "Turn the sound back on" | Mutes or unmutes whatever is playing. Music keeps its place silently; the radio's tuner stops until it is unmuted. Asking for something to play, turning the knob or pressing it also brings the sound back. |

The banner shows what was heard. Listening stops when you pause or press **Done Talking**
(left knob); **Cancel** (right knob) drops the request. Music is muted while the
microphone is open. Settings > Voice Assistant turns the wake word off.

The wake word and speech to text hear `godwit-mic`, as dictation does. It evens out a
near or far voice, takes out fan and road noise, and cancels what the head unit itself
is playing, so any plugged-in microphone does the job of a speakerphone.

Numbers can be said as digits or words; Whisper's usual mishearings ("for" for 4,
"to" for 2) still count as digits. Names are matched in the phone's contacts and then
its recent calls, so a phone that does not share its contacts can still be called back.
Contact addresses come from the phone's contacts (PBAP `ADR`, home first) and are only
used for a close name match, so a place is never taken for a person.

Patterns in `qml/Intents.js` understand most ways of asking straight away. Anything
else goes to Llama 3.2 1B on the Dragon Q6A's Hexagon NPU, which turns it into one of
these commands or says it is not one ("I can help with directions, calls, texts, music
and the radio."). It only interprets commands; it does not answer questions. On a board
without the NPU service, requests the patterns miss get that same answer. Listening for
the wake word stops during calls. Speech to text is the same whisper.cpp as dictation.

Everything runs on the head unit: the wake word (`godwit-wake`, about 4% of one core),
speech to text, the language model and the voice. They log under `godwit.wake` and
`godwit.llm`; the model service logs to `journalctl -u godwit-llm`.

| Variable | Default |
|---|---|
| `GODWIT_WAKE` | `1` turns the wake word on off the head unit |
| `GODWIT_MIC` | `godwit-mic` on the head unit (`godwit-hmi.service`); unset, PipeWire's default source |
| `GODWIT_WAKE_BIN` | `/usr/bin/godwit-wake` |
| `GODWIT_WAKE_DIR` | `/usr/share/godwit/wake` (the three models) |
| `GODWIT_LLM` | `1` uses the language model off the head unit |
| `GODWIT_LLM_URL` | `http://127.0.0.1:11434` |

In the simulator, **Hey Peregrine** pretends to hear one demo request after another,
and `--ask "Call Maya"` acts as if that request was said just after start-up.

## Licences

Ionicons (MIT) in `assets/icons/` (`trending-up-back.svg` is `trending-up`
mirrored, for bearing left), except `mode-fan.svg`, `seat-heat.svg` and
`seat-vent.svg`, which are Material Symbols (Apache-2.0, Google LLC). Inter
(OFL-1.1) in `assets/fonts/`.
The vehicle and water tank renders in `assets/vehicle/` and `assets/water/`
were generated for this project; each folder's README gives the prompts and
steps to make them again.
