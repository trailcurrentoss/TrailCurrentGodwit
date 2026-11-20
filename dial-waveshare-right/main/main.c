/*
 * Godwit dial, right side, on the Waveshare ESP32-S3-Knob-Touch-LCD-1.8.
 *
 * The firmware is ../dial-common/components/godwit_dial on the board layer
 * in ../dial-common/boards/waveshare-knob-1.8; this project only sets
 * CONFIG_GODWIT_DIAL_SIDE_RIGHT (sdkconfig.defaults).
 */

#include "godwit_dial.h"

void app_main(void)
{
    godwit_dial_start();
}
