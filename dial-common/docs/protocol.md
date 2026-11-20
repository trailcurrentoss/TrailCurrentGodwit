# The dial protocol

How a Godwit dial and the head unit talk over the dial's vendor-defined HID
interface. There are two copies of the numbers:
[`dial_protocol.h`](../components/dial_usb/include/dial_protocol.h) in the
firmware and [`hmi/src/dialprotocol.h`](../../hmi/src/dialprotocol.h) in the
HMI. Change both together, and bump the protocol version for any change an
older dial or HMI would misread.

## The USB device

| Field | Value |
|---|---|
| VID:PID | `303a:4005` (Espressif's VID, TinyUSB's default PID for CDC + HID; both dials share it) |
| Manufacturer / product | `TrailCurrent` / `Godwit Dial Left` or `Godwit Dial Right` |
| Serial number | the chip's MAC, 12 hex digits |
| Interfaces | CDC-ACM "Godwit dial log" (2), HID "Godwit dial" (1, interrupt IN + OUT, 64 bytes, 1 ms) |

Linux names the HID device `TrailCurrent Godwit Dial Left` (manufacturer +
product). The HMI looks for that name, then reads the side from the info
report, so it never depends on the port. Both boards the firmware runs on
(the Elecrow CrowPanel 1.28" and the Waveshare ESP32-S3-Knob-Touch-LCD-1.8)
present this same device, so the head unit takes either, or one of each.

## Reports

Each report is a report ID followed by 63 payload bytes, zero padded. The
exception is the knob event, which is 2 bytes. Multi-byte numbers are
little-endian. Text is UTF-8, NUL padded, and the sender cuts it on a
character boundary.

### Dial → head unit

| ID | Name | Payload |
|---|---|---|
| `0x01` | event | `kind, arg`. Kind 1 = switch down, 2 = switch up, 3 = turn, with `arg` = signed detents (clockwise positive). |
| `0x02` | icon request | `size px, name…`. The dial needs this Ionicon at this size. |
| `0x03` | info (feature) | `protocol version, side (0 left, 1 right), firmware version…, NUL, board name…`. The board name (`crowpanel-1.28`, `waveshare-knob-1.8`) follows the version's NUL; a head unit that reads only the version never sees it. |

### Head unit → dial

| ID | Name | Payload |
|---|---|---|
| `0x10` | ring | 5 × `r, g, b`, already scaled for brightness. LED 0 is lower-left, then round over the top to LED 4 lower-right (−144°, −72°, 0°, 72°, 144° from 12 o'clock). A board without LEDs (the Waveshare knob) draws the frame as a band round the edge of its display. |
| `0x11` | text | `field, utf8…`. The fields are 0 label, 1 value, 2 unit, 3 sub, 4 icon name. A field keeps its last value until it is sent again. |
| `0x12` | face | `flags, color, value px, icon px, pager count, pager on, backlight %, scale, scale steps, scale pos`. The flags are bit 0 dark theme and bit 1 bold value. The colours are 0 text, 1 accent, 2 heat, 3 cool, 4 warn. `pager on` is 255 for none. The scale is drawn round the edge (see below). **This is the commit**: the dial redraws with the text fields sent so far. |
| `0x13` | icon begin | `size px, name…` |
| `0x14` | icon data | `offset lo, offset hi, count, alpha bytes…`. Up to 60 bytes of the icon's `size × size` 8-bit alpha, row by row, in order. The icon is complete when its last byte arrives. |
| `0x15` | control | `cmd`. 1 = reboot into the ROM loader. |
| `0x16` | haptic | `strength`. One tick of the motor, 1 the lightest that can be felt to 255 the strongest. 0 is nothing. |

## Scales

A face can carry a scale round the edge of the display, open at the bottom
(from −135° to +135°, measured from 12 o'clock). The dial draws it itself in
the theme's colours, so it moves with the value at no cost on the wire.

| Scale | Drawn as | Used for |
|---|---|---|
| 0 none | nothing | |
| 1 split | a cool arc on the left and a heat arc on the right, cut on a slant at 12 o'clock, and a bead at step `pos` of `steps`. The middle step is neutral. | temperature, seat heat and vent |
| 2 dots | `steps` dots across the top, growing from left to right, the first `pos` lit in the accent colour | fan speed |
| 3 select | `steps` equal arc segments, segment `pos` lit in the accent colour | air flow |
| 4 ramp | the volume wedge: a band thin at the left end and thick at the right, filled up to `pos` of `steps` in the face's colour (the accent colour for text) | every volume |

A dial that predates a scale kind draws nothing for it. The three scale
bytes were added after version 1 shipped. Reports are zero
padded, so an older head unit's faces arrive with no scale, and an older dial
ignores the bytes; the protocol version did not change.

## The exchange

1. **The HMI opens the dial.** It reads the info report and checks the
   version. Then it sends every text field and a face report. It does this
   every time it opens a dial, because a dial forgets everything when it
   reboots.
2. **Ring frames.** These run continuously at ~30 a second, from the
   HMI's `ringFrame()`. They are also the HMI's heartbeat: after 1.5 s
   without one, the dial goes to standby until they resume.
3. **Display changes.** The HMI sends only the text fields that changed,
   then a face report. Sending the face report last means a half-updated
   display is never shown.
4. **Icons.** When a face names an icon the dial doesn't have at that size,
   the dial sends an icon request, and repeats it once a second until the
   icon arrives. The HMI renders the icon from the same SVG its screen uses
   and sends a begin report followed by data reports. An 84 px icon is 118
   reports, about 120 ms. The dial keeps the 48 most recently shown icons. An
   unknown name is answered with a blank icon so the dial stops asking.
5. **The knob.** Each detent and each switch edge is sent as it happens. The
   switch is debounced on the dial (6 ms). On the Waveshare knob, which has
   no switch, a finger on the glass is the switch: down when it lands and up
   when it lifts (debounced 20 ms), and the dial clicks its motor on down.
   Press, hold and turn-while-pressed are decided by the HMI.
6. **Haptic ticks.** For each turn report the HMI acts on, it sends one
   haptic report at the strength of where the value has got to. A control
   that has no strength (a list, say) sends none. The dial plays a tick only
   if its knob moved within the last 200 ms
   (`CONFIG_GODWIT_DIAL_HAPTIC_WINDOW_MS`), so a late tick is dropped and
   the motor is still once the knob is. Like a ring frame, a tick goes ahead
   of anything else waiting to be written, icon bytes included. A board
   without a motor ignores it, and an older dial ignores the report ID.

   | Control | Strength grows |
   |---|---|
   | every volume (media, call, guidance) | from 0 % up to 100 % |
   | temperature | from 72 °F, the middle of 60–84, out to either end |
   | seat heat and vent | from Off out to heat 3 or vent 3 |
   | fan | from Off and Auto up to Max |
   | map zoom | from the middle of the zoom range out to either limit |
   | radio tuning | with the signal of the frequency just tuned |

   The tuner measures a frequency only some time after it is tuned, so the
   radio's tick comes from the signal last measured there this session, by
   the band scan or by listening. A frequency not measured yet ticks at the
   lightest strength.

The HMI writes from a thread of its own, because a hidraw write waits for the
dial to take the report. Ring frames never queue: only the newest is kept,
and it goes ahead of any icon bytes still waiting.

## Flow control

The dial handles each output report inside TinyUSB's callback, and the OUT
endpoint is not re-armed until the callback returns. A dial that is busy
therefore holds off the host's next write instead of dropping reports.
