# Godwit dial firmware (shared)

Firmware for Godwit's two encoder modules: the knobs either side of the
screen, each with a round display. It runs on two kinds of module, and the
head unit takes either, or one of each:

| Board | What it is | Projects |
|---|---|---|
| **Elecrow CrowPanel 1.28" HMI ESP32 Rotary Display** | ESP32-S3, GC9A01 240×240, EC3501 ring encoder with a push switch, 5 × WS2812 LED ring | [`dial-left/`](../dial-left), [`dial-right/`](../dial-right) |
| **Waveshare ESP32-S3-Knob-Touch-LCD-1.8** | ESP32-S3, ST77916 360×360 touch display, a two-way detector switch worked by turning the housing, a DRV2605 vibration motor; no push switch, no LEDs | [`dial-waveshare-left/`](../dial-waveshare-left), [`dial-waveshare-right/`](../dial-waveshare-right) |

Both plug into the head unit over USB-C and present the same USB device, so
the head unit does not know which it has. Every dial runs the same firmware:
the four projects are each a few lines that pick a board and a side.
Everything else lives here.

## What a dial does

A dial makes no decisions of its own. The head unit's HMI owns every menu,
value and timing; the dial reports the knob and shows what the HMI sends:

| | Direction | What |
|---|---|---|
| Knob | dial → head unit | raw events: detents turned, switch down, switch up |
| Ring | head unit → dial | ~30 frames a second, five colours each |
| Display | head unit → dial | the same fields `KnobFace.qml` draws in the simulator: label, icon, value and unit, sub line, page dots, the scale round the edge (temperature, fan, air flow, the volume wedge), theme, brightness |
| Icons | head unit → dial | rendered from the HMI's own Ionicons SVGs when the dial asks for one |

Press, hold (550 ms) and turn-while-pressed are worked out by the HMI's
`Knobs` class from the raw events, the same way it does for the keyboard and
the simulator's on-screen knobs, so all three behave identically.

When the HMI isn't running, or the cable is out, the ring frames stop. After
1.5 s the dial shows its own standby face ("LEFT DIAL / Godwit / Waiting
for head unit") and breathes the ring green. When the frames resume, it goes
back to the HMI's face.

### The Waveshare knob

The Waveshare module has no push switch and no LED ring, so on that board:

- **The whole screen is the select button.** A finger anywhere on the glass
  is "switch down" and lifting it is "switch up"; the head unit decides
  press and hold from those exactly as it does for a real switch, so a tap
  selects, a touch held is a hold, and turning with a finger on the glass is
  turn-while-pressed. The DRV2605 clicks the motor on every touch down so the
  glass has a feel (`CONFIG_WSKNOB_HAPTIC`; the effect and motor type are
  in the board's Kconfig).
- **The motor ticks as the knob turns**, as hard as the head unit says:
  volume gets stronger towards 100 %, temperature, seat and fan towards
  their ends, map zoom towards its limits, and the radio follows the signal
  of the station tuned. It stops as soon as the knob does. A tick drives
  the motor in the DRV2605's real-time mode for 25 ms at an amplitude
  between `CONFIG_WSKNOB_HAPTIC_TURN_MIN` and `_MAX`; those are the
  settings to tune if the lightest ticks cannot be felt or the strongest
  are too much. See [docs/protocol.md](docs/protocol.md) for what each
  control sends.
- **The ring is drawn on the display**, as a band round the outside
  (`CONFIG_WSKNOB_SCREEN_RING`, 16 px), blended between the head unit's five
  model LEDs so a pattern that points somewhere still points there. The face
  is drawn inside it, scaled up from the 240 px design to what is left (328
  px), so the two kinds of dial look alike. Turning the band off gives the
  face the whole display.
- **The knob is not a quadrature encoder.** Turning the housing clicks an
  Alps SSCM110100 two-way detector switch: one contact closes for each step
  clockwise, the other for each step anticlockwise. The firmware counts the
  closings. If the head unit sees the knob going the wrong way, set
  `CONFIG_WSKNOB_KNOB_INVERT`.
- **The board carries two chips**, an ESP32-S3 and an ESP32, and the USB-C
  port reaches one at a time. The switch on the base must be on the
  **ESP32-S3** side, for the head unit as well as for flashing. The ESP32,
  the audio, the SD card and the battery are not used.

## How it talks to the head unit

The dial is a composite USB device:

- **Vendor-defined HID.** This carries the protocol. On the head unit it
  shows up as a hidraw node, and no keyboard or mouse device is created, so
  nothing a dial sends can reach another program. `hmi/src/dials.cpp` finds
  the dials by name ("TrailCurrent Godwit Dial Left/Right") and asks each one
  which side it is, so it doesn't matter which port either is plugged into,
  nor which board it is.
- **CDC serial.** This carries the ESP log, and it's how the flash script
  asks the firmware to reboot into the ROM loader.

[docs/protocol.md](docs/protocol.md) describes every report.

## Layout

```
components/godwit_dial/      the dial: protocol handling, face drawing, icon cache
components/dial_usb/         TinyUSB: vendor HID + CDC log, reboot-to-flash
  include/dial_protocol.h    the protocol; hmi/src/dialprotocol.h mirrors it
boards/crowpanel-1.28/       the Elecrow board: panel, knob, LED ring, backlight, LVGL
boards/waveshare-knob-1.8/   the Waveshare board: panel, knob, touch, haptics, on-screen ring
  <board>/dial_board/        the board component (every board provides dial_board.h)
  <board>/sdkconfig.defaults what the board adds to the shared configuration
sdkconfig.defaults           build configuration shared by every dial
partitions.csv               16 MB flash, one 4 MB app
scripts/flash.sh             flash a dial over its USB-C cable
docs/protocol.md             the protocol, in prose
tests/                       the CrowPanel quadrature decoder against a simulated encoder
```

`godwit_dial` is written against `dial_board.h` alone, and each board
provides a component named `dial_board` with that header. A project picks
its board by putting the board's folder on `EXTRA_COMPONENT_DIRS`, and its
side with `CONFIG_GODWIT_DIAL_SIDE_LEFT` or `_RIGHT`.

The text on the display is drawn with LVGL's TinyTTF from the HMI's own Inter
fonts (`hmi/assets/fonts/`, embedded at build time), so the dial matches the
simulator and can show any character a contact or network name contains.
Text is kept inside a circle clear of the scale and the display's curved
edge, and a face uses an icon instead of a label wherever one will do. The
face's measurements are KnobFace.qml's, for a 240 px display; a board with a
bigger one draws the same face scaled up (`DIAL_BOARD_FACE_PX`).

## Building and flashing

You need ESP-IDF 5.5. In any of the four project folders:

```bash
. $IDF_PATH/export.sh
cd dial-left                      # or dial-right, dial-waveshare-left, dial-waveshare-right
idf.py set-target esp32s3         # first build of a fresh clone only
idf.py build
../dial-common/scripts/flash.sh left        # left, right, waveshare-left, waveshare-right
```

`flash.sh` finds the running dial by its USB name, tells it to reboot into the
ROM loader, flashes it and opens the monitor on the new firmware. A board in
the ROM loader looks the same whichever side or board it is, so when flashing
one that has no working firmware yet, plug in only that one. Two running
dials of one side (an Elecrow left and a Waveshare left) have the same USB
name too; plug in one at a time.

If a dial never gets far enough to start USB, hold its **BOOT** button while
resetting it (on the Waveshare knob: hold BOOT, then power it on). It then
comes up in the ROM loader and `flash.sh` will flash it.

Configuration goes in `sdkconfig.defaults` (shared), the board's
`boards/<board>/sdkconfig.defaults`, or the project's own (only the side).
Never edit `sdkconfig`; it is generated. After changing a defaults file, run
`rm sdkconfig && idf.py build` in the project folder.

The defaults suit the enclosure, which holds both dials in the board's own
orientation. A case that mounts them upside down sets
`CONFIG_CROWPANEL_ROTATE_180=y` or `CONFIG_WSKNOB_ROTATE_180=y`, which turns
the picture and the ring round.

## On the head unit

- `hmi/src/dials.cpp` is the driver, used by `Knobs`.
- `Hmi.qml`'s `faces` sends each dial what its display should show.
- `ringFrame()` already calls `Knobs.setRing` 30 times a second.
- `os/meta-godwit/recipes-godwit/godwit-hmi/files/70-godwit-dials.rules`
  gives the `godwit` user access to the hidraw nodes.
- On a desktop running the simulator, the same rule gives the logged-in user
  access. Copy it to `/etc/udev/rules.d/`, then run
  `sudo udevadm control --reload` and replug the dials.

With dials plugged in, the simulator drives them too.
