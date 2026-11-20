/*
 * dial_board — the Waveshare ESP32-S3-Knob-Touch-LCD-1.8 (360x360, ESP32-S3),
 * as a Godwit dial board.
 *
 * THE BOARD INTERFACE. Every board the dial firmware runs on provides a
 * component named dial_board with this header, and godwit_dial is written
 * against it alone: the panel, the knob, the ring, the backlight and LVGL
 * on top, nothing else. The dial-* projects pick a board by putting its
 * folder under dial-common/boards/ on EXTRA_COMPONENT_DIRS. The other
 * board is dial-common/boards/crowpanel-1.28/.
 *
 * What this board is: the ST77916 QSPI panel (1.8", 360x360, round), a
 * CST816 capacitive touch controller over I2C, a knob that rotates the whole
 * display and clicks a two-way detector switch, a DRV2605 vibration motor
 * driver on the same I2C bus, and a MOSFET-switched backlight. There is no
 * push switch and no LED ring, so:
 *
 *   - The button is the touch screen. A finger anywhere on the glass is
 *     "down" and lifting it is "up"; the head unit decides press and hold
 *     from those exactly as it does for a switch. The DRV2605 clicks on
 *     touch down (CONFIG_WSKNOB_HAPTIC) so the glass has a feel.
 *   - The ring is drawn on the display, as a band round the outside
 *     (CONFIG_WSKNOB_SCREEN_RING), and the face is scaled into what is
 *     left. Or the ring frames are dropped and the face fills the display.
 *   - The knob is not a quadrature encoder. See src/board_input.c.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Reported to the head unit after the firmware version (DIAL_FEATURE_INFO). */
#define DIAL_BOARD_NAME     "waveshare-knob-1.8"

#define DIAL_BOARD_LCD_H_RES 360
#define DIAL_BOARD_LCD_V_RES 360

/**
 * The diameter the face is drawn into, centred on the display. KnobFace.qml
 * is designed for 240 px; the face is scaled up to this. With the on-screen
 * ring, the band round the edge is kept back for it.
 */
#ifdef CONFIG_WSKNOB_SCREEN_RING
#  define DIAL_BOARD_FACE_PX (DIAL_BOARD_LCD_V_RES - 2 * CONFIG_WSKNOB_SCREEN_RING_PX)
#else
#  define DIAL_BOARD_FACE_PX DIAL_BOARD_LCD_V_RES
#endif

/** LEDs in the head unit's ring model. See dial_board_ring_set(). */
#define DIAL_BOARD_RING_LEDS 5

/**
 * Bring up the panel, the touch controller, the knob, the motor driver and
 * LVGL.
 *
 * On return LVGL is running on its own task and the backlight is still OFF.
 * Draw the first screen, then call dial_board_backlight_set() so the panel
 * never shows an unpainted frame.
 */
esp_err_t dial_board_init(void);

/**
 * Take the LVGL mutex. Every lv_* call from a task other than LVGL's own must
 * be wrapped in this. LVGL callbacks (timers, event handlers) already hold
 * it -- taking it again there deadlocks.
 *
 * @param timeout_ms  0 waits forever.
 */
bool dial_board_lock(uint32_t timeout_ms);
void dial_board_unlock(void);

/** Backlight, 0-100 percent (LEDC PWM). */
esp_err_t dial_board_backlight_set(uint8_t percent);

/**
 * Show one frame of the ring. Safe from any task.
 *
 * rgb is DIAL_BOARD_RING_LEDS colours, 3 bytes each, in the head unit's
 * ring model: LED 0 lower-left, round over the top, to LED 4 lower-right, at
 * -144, -72, 0, 72 and 144 degrees from 12 o'clock. This board has no LEDs:
 * with CONFIG_WSKNOB_SCREEN_RING the frame is drawn as a band round the edge
 * of the display on the next LVGL tick, blended between the model LEDs;
 * without it the frame is dropped.
 */
void dial_board_ring_set(const uint8_t rgb[DIAL_BOARD_RING_LEDS * 3]);

/**
 * One haptic tick, strength 1 (lightest) to 255 (strongest). Safe from any
 * task; the input task plays it within 2 ms. The DRV2605 drives the motor
 * at an amplitude between CONFIG_WSKNOB_HAPTIC_TURN_MIN and _MAX for
 * CONFIG_WSKNOB_HAPTIC_TURN_MS, then stops it. A tick during a tick
 * restarts it at the new strength, so a fast spin is one buzz that ends
 * with the knob. Does nothing without CONFIG_WSKNOB_HAPTIC_TURN.
 */
void dial_board_haptic_tick(uint8_t strength);

/**
 * Knob input, delivered on the board's input task (NOT the LVGL task -- take
 * the display lock before touching the UI). Callbacks must not block.
 *
 * turn:   signed detents since the last call, clockwise positive. Relative
 *         only: the knob has no end stops.
 * button: the touch screen, debounced, true while a finger is on it.
 */
typedef void (*dial_board_turn_cb_t)(int detents, void *ctx);
typedef void (*dial_board_button_cb_t)(bool down, void *ctx);
void dial_board_set_input_callbacks(dial_board_turn_cb_t turn,
                                    dial_board_button_cb_t button,
                                    void *ctx);

#ifdef __cplusplus
}
#endif
