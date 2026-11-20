/*
 * The I2C bus: one master bus, the touch controller and the DRV2605 on it.
 * Only the input task uses it after start-up, so no locking of its own.
 */

#include "esp_check.h"
#include "esp_log.h"

#include "board_internal.h"
#include "board_pins.h"

static const char *TAG = "board.i2c";

/* Short: both devices answer within a byte time, and the input task polls
 * the touch controller every 10 ms, which must not stall on a missing
 * device. */
#define I2C_TIMEOUT_MS 20

static i2c_master_bus_handle_t s_bus;

esp_err_t board_i2c_init(void)
{
    const i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .glitch_ignore_cnt = 7,
        /* The board has 5K1 pull-ups; the internal ones (as the demo has
         * them) do no harm alongside. */
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "bus failed");
    ESP_LOGI(TAG, "SDA=%d SCL=%d at %d kHz", BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_HZ / 1000);
    return ESP_OK;
}

esp_err_t board_i2c_add(uint8_t addr, i2c_master_dev_handle_t *dev)
{
    ESP_RETURN_ON_FALSE(s_bus, ESP_ERR_INVALID_STATE, TAG, "no bus");
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BOARD_I2C_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &cfg, dev);
}

esp_err_t board_i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

esp_err_t board_i2c_read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(dev, &reg, 1, buf, n, I2C_TIMEOUT_MS);
}
