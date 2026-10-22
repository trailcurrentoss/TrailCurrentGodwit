/* Internal interface between the crowpanel_board translation units. */
#pragma once

#include "esp_err.h"
#include "lvgl.h"

esp_err_t board_display_init(lv_display_t **out_disp);
esp_err_t board_input_init(void);
esp_err_t board_leds_init(void);
