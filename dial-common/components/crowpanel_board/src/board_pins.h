/*
 * Elecrow CrowPanel 1.28" Rotary — 240x240 GC9A01 pin map.
 *
 * Source: github.com/Elecrow-RD/CrowPanel-1.28inch-HMI-ESP32-Rotary-Display-
 *         240-240-IPS-Round-Touch-Knob-Screen (branch `master`),
 *         example/Arduino/RotaryScreen_1_28/ — the LovyanGFX LGFX class.
 *         The wiki pin table agrees on every value. Verified on hardware in
 *         the TrailCurrent Capstan firmware and the Rotary HID macro pad.
 *
 * Despite "ESP32" in the product name this is an ESP32-S3R8 (8 MB octal
 * PSRAM, 16 MB flash), confirmed by reading the silicon over USB.
 *
 * If you change a number here, cite where it came from. A wrong GPIO in this
 * file presents as a dead panel or a dead encoder, and the debugging goes
 * looking at the driver.
 */
#pragma once

/* 4-wire SPI panel. */
#define BOARD_LCD_SPI_HOST      SPI2_HOST
#define BOARD_LCD_SPI_SCK       10
#define BOARD_LCD_SPI_MOSI      11
#define BOARD_LCD_SPI_DC        3
#define BOARD_LCD_SPI_CS        9
#define BOARD_LCD_RST           14
#define BOARD_LCD_SPI_HZ        (80 * 1000 * 1000)
#define BOARD_LCD_INVERT        1    /* cfg.invert = true */

/*
 * TWO RAILS MUST BE HIGH BEFORE ANY PANEL TRAFFIC.
 *
 * The vendor firmware drives GPIO1 and GPIO2 high in setup() with no
 * explanation. Without them the panel accepts no data and the board looks
 * dead -- no error, just a black screen.
 */
#define BOARD_LCD_RAIL_A        1
#define BOARD_LCD_RAIL_B        2

/* Backlight, LEDC-dimmable. */
#define BOARD_BL_GPIO           46
#define BOARD_BL_PWM_HZ         5000
#define BOARD_BL_PWM_BITS       8

/*
 * Rotary ring: EC3501 C15H30P3, 30 detents, contact chatter <= 5 ms.
 * Decoded by PCNT, full quadrature (4 edges per cycle). The phases have
 * EXTERNAL pull-ups -- the vendor uses bare INPUT on both -- so the internal
 * ones stay off.
 *
 * The whole panel is the push switch: pressing the glass presses the knob.
 */
#define BOARD_ENC_A             45
#define BOARD_ENC_B             42
#define BOARD_ENC_BTN           41   /* Active low. */

/*
 * WS2812 LED ring, 5 LEDs, GRB. Same vendor source as above. No supply
 * enable on this board; the ring is live as soon as the board is powered.
 */
#define BOARD_WS2812_GPIO       48
#define BOARD_WS2812_COUNT      5
/* Where each LED sits, in chain order, as degrees clockwise from 12 o'clock
 * seen from the front. Mapped on hardware in TrailCurrent Capstan by lighting
 * each index its own colour: 0 at 4 o'clock, 1 at 1, 2 at 11, 3 just shy of
 * 9, 4 at 6. board_leds.c resamples the head unit's ring model onto these. */
#define BOARD_WS2812_ANGLES     { 120, 30, -30, -85, 180 }

/*
 * Not driven by this firmware:
 *   GPIO40        power LED, active low
 *   GPIO5/6/7/13  CST816D touch controller -- the head unit takes no touch
 *                 input from the dials, only the knob
 * GPIO43/44 (UART0) are free on this board and carry the ESP console -- see
 * sdkconfig.defaults.
 */
