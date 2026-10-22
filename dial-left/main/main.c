/*
 * Godwit dial, left side.
 *
 * The firmware is ../dial-common/components/godwit_dial; this project only
 * sets CONFIG_GODWIT_DIAL_SIDE_LEFT (sdkconfig.defaults).
 */

#include "godwit_dial.h"

void app_main(void)
{
    godwit_dial_start();
}
