# Godwit Left Dial Firmware (Waveshare)

ESP-IDF firmware for the left-hand encoder module when it is a **Waveshare
ESP32-S3-Knob-Touch-LCD-1.8**: the knob, its 1.8" round touch display and its
vibration motor. The module connects to the head unit over USB and shows up
there as a HID device, exactly like the Elecrow dial in
[`dial-left/`](../dial-left); the head unit does not know which it has,
and one of each is fine.

This dial has no push switch. **The whole screen is the select button**: a
finger anywhere on the glass is a press, and the motor clicks under it.

All of the firmware lives in [`dial-common/`](../dial-common). This project
only builds it for this board (`boards/waveshare-knob-1.8`) and the left
side (`CONFIG_GODWIT_DIAL_SIDE_LEFT` in `sdkconfig.defaults`).

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3     # first build of a fresh clone only
idf.py build
../dial-common/scripts/flash.sh waveshare-left
```

The board has two chips on it and the USB-C port reaches one at a time: the
switch on the base must be on the **ESP32-S3** side, for the head unit as
well as for flashing.

See [dial-common/README.md](../dial-common/README.md) for what the dial does,
how it talks to the head unit, and what is different about this board.
