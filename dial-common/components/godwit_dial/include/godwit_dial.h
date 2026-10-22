/*
 * godwit_dial — the firmware of one Godwit encoder module.
 *
 * Which dial it is (left or right) is CONFIG_GODWIT_DIAL_SIDE, set by the
 * dial-left and dial-right projects. Everything else is the same for both.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Bring up the board, the display and USB, and start serving the head unit. */
void godwit_dial_start(void);

#ifdef __cplusplus
}
#endif
