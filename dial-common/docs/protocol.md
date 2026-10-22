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
report, so it never depends on the port.

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
| `0x03` | info (feature) | `protocol version, side (0 left, 1 right), firmware version…` |

### Head unit → dial

| ID | Name | Payload |
|---|---|---|
| `0x10` | ring | 5 × `r, g, b`, already scaled for brightness. LED 0 is lower-left, then round over the top to LED 4 lower-right (−144°, −72°, 0°, 72°, 144° from 12 o'clock). |
| `0x11` | text | `field, utf8…`. The fields are 0 label, 1 value, 2 unit, 3 sub, 4 icon name. A field keeps its last value until it is sent again. |
| `0x12` | face | `flags, color, value px, icon px, pager count, pager on, backlight %`. The flags are bit 0 dark theme and bit 1 bold value. The colours are 0 text, 1 accent, 2 heat, 3 cool, 4 warn. `pager on` is 255 for none. **This is the commit**: the dial redraws with the text fields sent so far. |
| `0x13` | icon begin | `size px, name…` |
| `0x14` | icon data | `offset lo, offset hi, count, alpha bytes…`. Up to 60 bytes of the icon's `size × size` 8-bit alpha, row by row, in order. The icon is complete when its last byte arrives. |
| `0x15` | control | `cmd`. 1 = reboot into the ROM loader. |

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
   switch is debounced on the dial (6 ms). Press, hold and
   turn-while-pressed are decided by the HMI.

The HMI writes from a thread of its own, because a hidraw write waits for the
dial to take the report. Ring frames never queue: only the newest is kept,
and it goes ahead of any icon bytes still waiting.

## Flow control

The dial handles each output report inside TinyUSB's callback, and the OUT
endpoint is not re-armed until the callback returns. A dial that is busy
therefore holds off the host's next write instead of dropping reports.
