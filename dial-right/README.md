# Godwit Right Dial Firmware

ESP-IDF firmware for the right-hand encoder module: the knob, its round
display and its LED ring. The module connects to the head unit over USB and
shows up there as a HID device.

Both dials run the same firmware, which lives in
[`dial-common/`](../dial-common). This project only builds it for the right
side (`CONFIG_GODWIT_DIAL_SIDE_RIGHT` in `sdkconfig.defaults`).

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3     # first build of a fresh clone only
idf.py build
../dial-common/scripts/flash.sh right
```

See [dial-common/README.md](../dial-common/README.md) for what the dial does
and how it talks to the head unit.
