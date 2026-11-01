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
| `src/music.*`, `src/streaming.*` | The Media app's sources (`Music` in QML): the head unit's own music in `/data/media/music` (Local) and the music on a USB drive, played through Qt Multimedia; Spotify (go-librespot), Apple Music (AirPlay, shairport-sync) and YouTube Music ([../ytmusic](../ytmusic)), each run by the HMI as a helper program while it is turned on. See [Music sources](#music-sources) |
| `src/wake.*` | The wake word (`Wake` in QML): `pw-record` from PipeWire's default microphone into `godwit-wake` ([../wake](../wake)), which listens for "Hey Peregrine" |
| `src/llm.*` | The language model (`Llm` in QML): sends a spoken request the patterns miss to the NPU model service on the head unit (`godwit-llm`, [../llm](../llm), `127.0.0.1:11434`) and returns the command it wrote |
| `qml/Intents.js` | The voice assistant's four commands: the patterns that understand them, contact matching, and the check on what the language model wrote |
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

### Music sources

The Media app plays from six sources, chosen with the buttons along the
bottom of the screen or by holding the right knob. One plays at a time:
choosing another, tuning the radio or a phone starting its music pauses the
rest. A call pauses the music and resumes it when the call ends.

| Source | Plays | Needs |
|---|---|---|
| **Local** | Music imported into `/data/media/music`. Browse by All Songs, album or artist. | Nothing. **Import from USB** copies a drive's new songs in, as Pictures and Videos do. |
| **USB** | The music on a plugged-in drive, straight from the drive | A drive (mounted read-only under `/run/godwit-usb`) |
| **Bluetooth** | The connected phone's music (A2DP and AVRCP) | A paired phone |
| **Spotify** | Search (songs, artists, albums, playlists), radio stations from a song or an artist, the account's playlists and Liked Songs, Up Next, and anything sent to the head unit from the Spotify app (it appears there as a Spotify Connect device) | Spotify Premium, the internet, and signing in once (below) |
| **Apple Music** | Music an iPhone sends over AirPlay. Apple offers no way to sign in to Apple Music on a non-Apple head unit, so the iPhone plays and the head unit is its speaker; play, pause and skip go back to the phone. | The iPhone and head unit on the same Wi-Fi network (the iPhone's Personal Hotspot works) |
| **YouTube Music** | Search, then a song plays with a radio of songs like it as Up Next; signed in, the account's playlists and Liked Music | The internet. Signing in is optional (below). |

**Browse** on the right card holds a source's search results, then its
playlists, albums or artists; the right knob turns through them and presses
to play, in any gear. Typing a search needs Park, as destination search does.
For voice, `Hmi.playMusic(words, source)` plays the best match from a source
(the one heard now when it can search, if none is named): on Spotify the
artist named, or else the top song with a radio after it; on YouTube Music
the top song with its radio; on Local and USB an artist or album by name,
or else the songs whose titles match.

Spotify's search uses Spotify's Web API. Spotify rate-limits the token
go-librespot holds for the session heavily, since every librespot-based
player shares its client ID, so searches with it often come back "busy". For
reliable search, give the head unit a Spotify app of your own: at
[developer.spotify.com/dashboard](https://developer.spotify.com/dashboard),
**Create app** (any name and description; Redirect URI
`http://127.0.0.1:8888/callback`, which is required but not used; tick
**Web API**), then enter its **Client ID** and **Client Secret** as **Search
App Client ID** and **Search App Client Secret** in Spotify's settings. The
head unit gets a token for it by the client credentials flow, with no
browser or sign-in. Spotify requires the app's owner to have Premium. When
Spotify says to wait (HTTP 429), no search is sent until its Retry-After has
passed, and the driver is told Spotify is busy rather than that nothing was
found. Meanwhile a spoken "Play …" falls back to the account's own music: a
playlist by name, else an artist or song in Liked Songs, started as a radio.
Its radios are Spotify's own stations (`spotify:station:track:…`,
`spotify:station:artist:…`). Up Next lists what the playing playlist or album
holds; a station, or an empty Liked Songs, has no listing, and Up Next then
stays empty while the music plays on.

Spotify, Apple Music and YouTube Music each have a **settings** page (the
gear on the right card), where they are turned on or off and their accounts
and names are set. Typing and signing in need Park.

- **Spotify** is off until turned on, so the head unit contacts Spotify only
  once asked to. Turning it on shows a code: on any phone or computer, go to
  **spotify.com/pair** and enter it. The sign-in is kept until **Sign Out**.
  **Device Name** is how the head unit appears in the Spotify app. **Search
  App Client ID** and **Secret** are optional and make search reliable (above).
- **Apple Music** has an **AirPlay Name** and an optional **AirPlay
  Password**, which the iPhone asks for the first time it connects. It uses
  classic AirPlay, which supports a password and remote control.
- **YouTube Music** plays without an account. To sign in, Google requires an
  OAuth client of your own: in [Google Cloud Console](https://console.cloud.google.com/),
  create a project, enable the **YouTube Data API v3**, and create an OAuth
  client ID of type **TVs and Limited Input devices**. Enter its client ID and
  secret, tap **Sign In**, then enter the code shown at **google.com/device**
  on a phone.

Settings and sign-ins are kept in `/var/lib/godwit` (the service's
`StateDirectory`, mode 0700), readable by the HMI only. The helpers' sound
goes to ALSA's default device, which is PipeWire, so the media volume and
mute apply to every source.

These three sources are optional extras that need the network; nothing else
on the head unit depends on them, and each degrades to a message when its
helper is missing or offline. Spotify (go-librespot) and YouTube Music
(ytmusicapi and yt-dlp) use unofficial clients that the services' terms do
not permit and that can stop working whenever the services change. YouTube
in particular now rejects many direct stream requests, so YouTube Music
playback may fail even where search works. go-librespot's Go modules are
pinned by a generated file: run `bitbake -c update_modules go-librespot` once
before the first image build, and again after changing its version.

| Variable | Default |
|---|---|
| `GODWIT_MUSIC_DIR` | `/data/media/music` |
| `GODWIT_SPOTIFY_BIN`, `GODWIT_SPOTIFY_PORT` | `go-librespot` on the PATH, API port `3678` on 127.0.0.1 |
| `GODWIT_AIRPLAY_BIN` | `shairport-sync` on the PATH; `GODWIT_AIRPLAY_BUS=session` looks for its MPRIS interface on the session bus |
| `GODWIT_YTMUSIC_BIN` | `godwit-ytmusic` on the PATH |
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

### Voice assistant

Say **"Hey Peregrine"**, wait for the chime, then ask for one of these:

| Say | Does |
|---|---|
| "Get directions to Arches National Park", "Take me to the nearest gas station", "Directions to Maya's house" | Searches for the place, or for a contact's address when the place is a contact with one on the phone, and opens the route preview of the first match, saying how far and how long. **Press** the right knob to start, as with a place picked by hand. |
| "Call Maya", "Call 435 555 0142", "Dial four three five…" | Calls the contact (the closest name in the phone's contacts or recent calls) or the number |
| "Send a text to Jordan", "Text Maya I'm running late", "Send a message to 801 555 1212 saying on my way" | Asks for the message unless it was said, reads it back, and **Send** texts it, as a reply to a message does |
| "Set the radio to 101.9", "Radio preset 3", "Put on AM 880" | Tunes the radio |
| "Play Taylor Swift on Spotify", "Put on some Johnny Cash", "Play Rumours from my library" | Plays it on the named source, or the one playing, or the first that is set up (`Hmi.playMusic`) |

The banner shows what was heard. Listening stops when you pause or press **Done Talking**
(left knob); **Cancel** (right knob) drops the request. Music is muted while the
microphone is open. Settings > Voice Assistant turns the wake word off.

Numbers can be said as digits or words; Whisper's usual mishearings ("for" for 4,
"to" for 2) still count as digits. Names are matched in the phone's contacts and then
its recent calls, so a phone that does not share its contacts can still be called back.
Contact addresses come from the phone's contacts (PBAP `ADR`, home first) and are only
used for a close name match, so a place is never taken for a person.

Patterns in `qml/Intents.js` understand most ways of asking straight away. Anything
else goes to Llama 3.2 1B on the Dragon Q6A's Hexagon NPU, which turns it into one of
the four commands or says it is not one ("I can help with directions, calls, texts and
the radio."). It only interprets commands; it does not answer questions. On a board
without the NPU service, requests the patterns miss get that same answer. Listening for
the wake word stops during calls. Speech to text is the same whisper.cpp as dictation.

Everything runs on the head unit: the wake word (`godwit-wake`, about 4% of one core),
speech to text, the language model and the voice. They log under `godwit.wake` and
`godwit.llm`; the model service logs to `journalctl -u godwit-llm`.

| Variable | Default |
|---|---|
| `GODWIT_WAKE` | `1` turns the wake word on off the head unit |
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
