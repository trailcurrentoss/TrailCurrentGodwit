/*
 * The CST816 touch controller, read as one button: a finger on the glass,
 * or none. Where it is does not matter -- the whole screen is the select
 * button of this dial.
 *
 * Only the finger count is read (register 0x02, as Waveshare's demo reads
 * it along with the coordinates). The controller is told not to sleep, so
 * the input task's polls are always answered; if a part ignores that and
 * sleeps anyway, a touch wakes it and the next poll sees the finger -- an
 * unanswered poll is reported as an error and the caller treats it as no
 * finger.
 */

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_internal.h"
#include "board_pins.h"

static const char *TAG = "board.touch";

#define REG_MODE            0x00    /* the demo writes 0 here: normal mode */
#define REG_FINGER_NUM      0x02
#define REG_CHIP_ID         0xA7    /* then project id, firmware version */
#define REG_DIS_AUTO_SLEEP  0xFE    /* 1: never enter standby */

static i2c_master_dev_handle_t s_dev;

esp_err_t board_touch_init(void)
{
    /* Reset it. The demo leaves RST alone (the controller has its own
     * pull-up); a pulse costs 10 ms and starts it from a known state. */
    const gpio_config_t rst = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BOARD_TOUCH_RST,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst), TAG, "rst gpio failed");
    gpio_set_level(BOARD_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));       /* the datasheet's wake-up time */

    /* INT is not used -- the controller is polled -- but is an input, not
     * left floating. */
    const gpio_config_t irq = {
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << BOARD_TOUCH_INT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&irq), TAG, "int gpio failed");

    ESP_RETURN_ON_ERROR(board_i2c_add(BOARD_TOUCH_ADDR, &s_dev), TAG, "add device");

    uint8_t id[3] = { 0 };
    esp_err_t err = board_i2c_read_regs(s_dev, REG_CHIP_ID, id, sizeof(id));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no answer from the touch controller at 0x%02x", BOARD_TOUCH_ADDR);
        s_dev = NULL;
        return err;
    }
    ESP_RETURN_ON_ERROR(board_i2c_write_reg(s_dev, REG_MODE, 0x00), TAG, "mode");
    ESP_RETURN_ON_ERROR(board_i2c_write_reg(s_dev, REG_DIS_AUTO_SLEEP, 0x01), TAG, "no sleep");

    ESP_LOGI(TAG, "CST816 chip 0x%02x project 0x%02x firmware %u, the whole screen is the button",
             id[0], id[1], id[2]);
    return ESP_OK;
}

esp_err_t board_touch_read(bool *down)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t n = 0;
    esp_err_t err = board_i2c_read_regs(s_dev, REG_FINGER_NUM, &n, 1);
    if (err != ESP_OK) {
        return err;
    }
    *down = (n & 0x0F) != 0;
    return ESP_OK;
}
