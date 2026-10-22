# Godwit dial firmware (shared)

Firmware for Godwit's two encoder modules: the knobs either side of the
screen, each with a 1.28" round display and a ring of five RGB LEDs. The
module is an **Elecrow CrowPanel 1.28" HMI ESP32 Rotary Display** (ESP32-S3,
GC9A01 240×240, EC3501 ring encoder, 5 × WS2812). It plugs into the head
unit over USB-C.

The two dials run the same firmware. [`dial-left/`](../dial-left) and
[`dial-right/`](../dial-right) are ESP-IDF projects that build it for one
side or the other. Everything else lives here.

## What a dial does

A dial makes no decisions of its own. The head unit's HMI owns every menu,
value and timing; the dial reports the knob and shows what the HMI sends:

| | Direction | What |
|---|---|---|
| Knob | dial → head unit | raw events: detents turned, switch down, switch up |
| Ring | head unit → dial | ~30 frames a second, five colours each |
| Display | head unit → dial | the same fields `KnobFace.qml` draws in the simulator: label, icon, value and unit, sub line, page dots, theme, brightness |
| Icons | head unit → dial | rendered from the HMI's own Ionicons SVGs when the dial asks for one |

Press, hold (550 ms) and turn-while-pressed are worked out by the HMI's
`Knobs` class from the raw events, the same way it does for the keyboard and
the simulator's on-screen knobs, so all three behave identically.

When the HMI isn't running, or the cable is out, the ring frames stop. After
1.5 s the dial shows its own standby face ("LEFT DIAL / Godwit / Waiting
for head unit") and breathes the ring green. When the frames resume, it goes
back to the HMI's face.

## How it talks to the head unit

The dial is a composite USB device:

- **Vendor-defined HID.** This carries the protocol. On the head unit it
  shows up as a hidraw node, and no keyboard or mouse device is created, so
  nothing a dial sends can reach another program. `hmi/src/dials.cpp` finds
  the dials by name ("TrailCurrent Godwit Dial Left/Right") and asks each one
  which side it is, so it doesn't matter which port either is plugged into.
- **CDC serial.** This carries the ESP log, and it's how the flash script
  asks the firmware to reboot into the ROM loader.

[docs/protocol.md](docs/protocol.md) describes every report.

## Layout

```
components/crowpanel_board/  panel, knob, LED ring, backlight, LVGL (CrowPanel 1.28" only)
components/dial_usb/         TinyUSB: vendor HID + CDC log, reboot-to-flash
  include/dial_protocol.h    the protocol; hmi/src/dialprotocol.h mirrors it
components/godwit_dial/      the dial: protocol handling, face drawing, icon cache
sdkconfig.defaults           build configuration shared by both dials
partitions.csv               16 MB flash, one 4 MB app
scripts/flash.sh             flash a dial over its USB-C cable
docs/protocol.md             the protocol, in prose
```

The text on the display is drawn with LVGL's TinyTTF from the HMI's own Inter
fonts (`hmi/assets/fonts/`, embedded at build time), so the dial matches the
simulator and can show any character a contact or network name contains.

## Building and flashing

You need ESP-IDF 5.5. In either project folder:

```bash
. $IDF_PATH/export.sh
cd dial-left                      # or dial-right
idf.py set-target esp32s3         # first build of a fresh clone only
idf.py build
../dial-common/scripts/flash.sh left
```

`flash.sh` finds the running dial by its USB name, tells it to reboot into the
ROM loader, flashes it and opens the monitor on the new firmware. A board in
the ROM loader looks the same whichever side it is, so when flashing one that
has no working firmware yet, plug in only that one.

If a dial never gets far enough to start USB, hold its **BOOT** button while
resetting it. It then comes up in the ROM loader and `flash.sh` will flash it.

Configuration goes in `sdkconfig.defaults` (shared) or the project's own
`sdkconfig.defaults` (only the side). Never edit `sdkconfig`; it is
generated. After changing a defaults file, run `rm sdkconfig && idf.py build`
in the project folder.

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
