# Godwit Left Dial Firmware

ESP-IDF firmware for the left-hand encoder module: the knob, its round
display and its LED ring. The module connects to the head unit over USB and
shows up there as a HID device.

Every dial runs the same firmware, which lives in
[`dial-common/`](../dial-common). This project only builds it for this
board, the Elecrow CrowPanel 1.28" Rotary (`boards/crowpanel-1.28`), and the
left side (`CONFIG_GODWIT_DIAL_SIDE_LEFT` in `sdkconfig.defaults`). The
same module can instead be a Waveshare ESP32-S3-Knob-Touch-LCD-1.8, built
from [`dial-waveshare-left/`](../dial-waveshare-left); the head unit takes
either, or one of each.

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3     # first build of a fresh clone only
idf.py build
../dial-common/scripts/flash.sh left
```

See [dial-common/README.md](../dial-common/README.md) for what the dial does
and how it talks to the head unit.
