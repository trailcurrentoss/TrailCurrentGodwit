/*
 * Display bring-up: GC9A01 240x240 on a 4-wire SPI bus.
 *
 * board_display_init() returns an LVGL display owned by esp_lvgl_port. The
 * backlight is NOT switched on here -- crowpanel_board_init() leaves it dark
 * until the first screen has been drawn.
 */

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "board_internal.h"
#include "board_pins.h"
#include "crowpanel_board.h"

static const char *TAG = "board.disp";

/* Kconfig bools that are 'n' are UNDEFINED, not 0, so they cannot appear in
 * a runtime expression. Normalise once. */
#ifdef CONFIG_CROWPANEL_LCD_BGR
#  define LCD_BGR 1
#else
#  define LCD_BGR 0
#endif
#ifdef CONFIG_CROWPANEL_LCD_MIRROR_X
#  define LCD_MIRROR_X 1
#else
#  define LCD_MIRROR_X 0
#endif
#ifdef CONFIG_CROWPANEL_LCD_MIRROR_Y
#  define LCD_MIRROR_Y 1
#else
#  define LCD_MIRROR_Y 0
#endif

/*
 * Partial-buffer rendering: two buffers of this many pixels, in INTERNAL
 * DMA-capable RAM. 8192 px = 16 KB each. Moving them to PSRAM was tried in
 * Capstan: the SPI flush then runs slowly enough to starve IDLE1 and trip
 * the task watchdog on the LVGL task. SPI is the bottleneck here, not
 * memory, and smaller buffers let LVGL start flushing sooner anyway.
 */
#define DRAW_BUF_PX 8192

static esp_lcd_panel_handle_t    s_panel;
static esp_lcd_panel_io_handle_t s_io;

/* GPIO1 and GPIO2 must be high before the panel accepts anything. Without
 * them it ignores every byte and the board looks dead -- see board_pins.h. */
static esp_err_t enable_panel_rails(void)
{
    const gpio_config_t cfg = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BOARD_LCD_RAIL_A) | (1ULL << BOARD_LCD_RAIL_B),
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "rail gpio failed");
    gpio_set_level(BOARD_LCD_RAIL_A, 1);
    gpio_set_level(BOARD_LCD_RAIL_B, 1);
    return ESP_OK;
}

esp_err_t board_display_init(lv_display_t **out_disp)
{
    ESP_RETURN_ON_ERROR(enable_panel_rails(), TAG, "panel rails failed");

    /* One colour transaction is capped at a full-width 80-row band. */
    const spi_bus_config_t bus = {
        .sclk_io_num     = BOARD_LCD_SPI_SCK,
        .mosi_io_num     = BOARD_LCD_SPI_MOSI,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = CROWPANEL_LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO),
                        TAG, "spi bus init failed");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num       = BOARD_LCD_SPI_CS,
        .dc_gpio_num       = BOARD_LCD_SPI_DC,
        .spi_mode          = 0,
        .pclk_hz           = BOARD_LCD_SPI_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST,
                                 &io_cfg, &s_io),
        TAG, "spi panel io failed");

    const esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = BOARD_LCD_RST,
        /* From Kconfig, not from the vendor's rgb_order -- see
         * CROWPANEL_LCD_BGR. */
        .rgb_ele_order  = LCD_BGR ? LCD_RGB_ELEMENT_ORDER_BGR
                                  : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9a01(s_io, &dev_cfg, &s_panel),
                        TAG, "gc9a01 panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),  TAG, "init failed");
#if BOARD_LCD_INVERT
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG,
                        "invert failed");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG,
                        "display on failed");

    /*
     * Orientation is NOT set on the panel here. lvgl_port_add_disp() applies
     * its own cfg.rotation by calling esp_lcd_panel_mirror() and
     * esp_lcd_panel_swap_xy(), which silently overwrites anything set
     * beforehand with its defaults of zero. Set it in disp_cfg.rotation.
     */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = s_io,
        .panel_handle  = s_panel,
        .buffer_size   = DRAW_BUF_PX,
        .double_buffer = true,
        .hres          = CROWPANEL_LCD_H_RES,
        .vres          = CROWPANEL_LCD_V_RES,
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = LCD_MIRROR_X,
            .mirror_y = LCD_MIRROR_Y,
        },
        .flags = {
            .buff_dma    = true,
            .buff_spiram = false,
            .swap_bytes  = true,   /* SPI panels take big-endian RGB565. */
        },
    };
    *out_disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(*out_disp, ESP_FAIL, TAG, "lvgl_port_add_disp failed");

    ESP_LOGI(TAG, "GC9A01 %dx%d SPI up @ %d MHz, %s, mirror_x=%d mirror_y=%d",
             CROWPANEL_LCD_H_RES, CROWPANEL_LCD_V_RES, BOARD_LCD_SPI_HZ / 1000000,
             LCD_BGR ? "BGR" : "RGB", LCD_MIRROR_X, LCD_MIRROR_Y);
    return ESP_OK;
}
