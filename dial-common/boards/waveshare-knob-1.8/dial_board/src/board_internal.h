/* Internal interface between the Waveshare dial_board translation units. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "lvgl.h"

esp_err_t board_display_init(lv_display_t **out_disp);

/* The I2C bus the touch controller and the motor driver share. */
esp_err_t board_i2c_init(void);
esp_err_t board_i2c_add(uint8_t addr, i2c_master_dev_handle_t *dev);
esp_err_t board_i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val);
esp_err_t board_i2c_read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t n);

/* Touch: the whole screen as one button. board_touch_read() fails when the
 * controller does not answer, which it may not while asleep. */
esp_err_t board_touch_init(void);
esp_err_t board_touch_read(bool *down);

/* Haptics. board_haptic_click() does nothing if the driver was not found.
 * board_haptic_poll() runs on the input task every poll: it starts and ends
 * the turn ticks dial_board_haptic_tick() asks for. */
esp_err_t board_haptic_init(void);
void      board_haptic_click(void);
void      board_haptic_poll(void);

esp_err_t board_input_init(void);

/* The on-screen ring. Takes the LVGL lock itself. */
esp_err_t board_ring_init(lv_display_t *disp);
