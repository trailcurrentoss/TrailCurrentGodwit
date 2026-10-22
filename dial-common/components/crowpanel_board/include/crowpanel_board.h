/*
 * crowpanel_board — Elecrow CrowPanel 1.28" Rotary (240x240, ESP32-S3).
 *
 * The only hardware-aware code in the dial firmware: the GC9A01 SPI panel,
 * the rotary ring and its push switch, the WS2812 LED ring, the backlight,
 * and LVGL on top of the panel via esp_lvgl_port.
 *
 * Ported from the Rotary HID macro pad's board layer (which came from
 * TrailCurrent Capstan). The differences are what a Godwit dial needs:
 *
 *   - The knob reports RAW events: detents, and the switch going down and
 *     up. Press, hold and turn-while-pressed are decided by the head unit,
 *     so both dials, the keyboard and the simulator share one set of timings.
 *   - The LED ring takes a whole frame, five colours at once, in the head
 *     unit's ring model, and maps it onto where the LEDs physically sit.
 *   - No touch. The head unit takes no touch input from the dials.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CROWPANEL_LCD_H_RES 240
#define CROWPANEL_LCD_V_RES 240

/** LEDs in the head unit's ring model. See crowpanel_board_ring_set(). */
#define CROWPANEL_RING_LEDS 5

/**
 * Bring up the panel, the knob, the LED ring and LVGL.
 *
 * On return LVGL is running on its own task and the backlight is still OFF.
 * Draw the first screen, then call crowpanel_board_backlight_set() so the
 * panel never shows an unpainted frame.
 */
esp_err_t crowpanel_board_init(void);

/**
 * Take the LVGL mutex. Every lv_* call from a task other than LVGL's own must
 * be wrapped in this. LVGL callbacks (timers, event handlers) already hold
 * it -- taking it again there deadlocks.
 *
 * @param timeout_ms  0 waits forever.
 */
bool crowpanel_board_lock(uint32_t timeout_ms);
void crowpanel_board_unlock(void);

/** Backlight, 0-100 percent (LEDC PWM). */
esp_err_t crowpanel_board_backlight_set(uint8_t percent);

/**
 * Show one frame on the LED ring. Safe from any task.
 *
 * rgb is CROWPANEL_RING_LEDS colours, 3 bytes each, in the head unit's ring
 * model: LED 0 lower-left, round over the top, to LED 4 lower-right, at
 * -144, -72, 0, 72 and 144 degrees from 12 o'clock. The board's LEDs are not
 * at those angles, so each physical LED shows the model blended at its own
 * angle. Scaled by CONFIG_CROWPANEL_LEDS_MAX_BRIGHTNESS; repeating the frame
 * on the ring costs nothing.
 */
void crowpanel_board_ring_set(const uint8_t rgb[CROWPANEL_RING_LEDS * 3]);

/**
 * Knob input, delivered on the board's input task (NOT the LVGL task -- take
 * the display lock before touching the UI). Callbacks must not block.
 *
 * turn:   signed detents since the last call, clockwise positive. Relative
 *         only: the ring has no end stops.
 * button: the push switch, debounced, true when it closes.
 */
typedef void (*crowpanel_turn_cb_t)(int detents, void *ctx);
typedef void (*crowpanel_button_cb_t)(bool down, void *ctx);
void crowpanel_board_set_input_callbacks(crowpanel_turn_cb_t turn,
                                         crowpanel_button_cb_t button,
                                         void *ctx);

#ifdef __cplusplus
}
#endif
