/*
 * Display bring-up: ST77916 360x360 on a QSPI bus.
 *
 * board_display_init() returns an LVGL display owned by esp_lvgl_port. The
 * backlight is NOT switched on here -- dial_board_init() leaves it dark
 * until the first screen has been drawn.
 *
 * The driver is espressif/esp_lcd_st77916 with the panel's own init table
 * (board_lcd_init.h); its default table is for another panel.
 */

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "board_internal.h"
#include "board_lcd_init.h"
#include "board_pins.h"
#include "dial_board.h"

static const char *TAG = "board.disp";

/* Kconfig bools that are 'n' are UNDEFINED, not 0, so they cannot appear in
 * a runtime expression. Normalise once. */
#ifdef CONFIG_WSKNOB_LCD_BGR
#  define LCD_BGR 1
#else
#  define LCD_BGR 0
#endif
#ifdef CONFIG_WSKNOB_LCD_MIRROR_X
#  define LCD_MIRROR_X 1
#else
#  define LCD_MIRROR_X 0
#endif
#ifdef CONFIG_WSKNOB_LCD_MIRROR_Y
#  define LCD_MIRROR_Y 1
#else
#  define LCD_MIRROR_Y 0
#endif
/* A 180-degree turn is both mirrors flipped from upright. */
#ifdef CONFIG_WSKNOB_ROTATE_180
#  define LCD_ROTATE_180 1
#else
#  define LCD_ROTATE_180 0
#endif

/*
 * Partial-buffer rendering: two buffers of this many rows, in INTERNAL
 * DMA-capable RAM (PSRAM buffers starve the flush; see the CrowPanel board).
 * 40 rows of 360 px is 28.8 KB each. QSPI moves four bits a clock, so a
 * band this size flushes in well under a millisecond.
 */
#define DRAW_ROWS   40
#define DRAW_BUF_PX (DIAL_BOARD_LCD_H_RES * DRAW_ROWS)

static esp_lcd_panel_handle_t    s_panel;
static esp_lcd_panel_io_handle_t s_io;

/* The panel takes windows that start on an even column and row and end on
 * an odd one; Waveshare's demo rounds every area the same way. */
static void round_area(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

esp_err_t board_display_init(lv_display_t **out_disp)
{
    const spi_bus_config_t bus = ST77916_PANEL_BUS_QSPI_CONFIG(
        BOARD_LCD_QSPI_SCK, BOARD_LCD_QSPI_D0, BOARD_LCD_QSPI_D1,
        BOARD_LCD_QSPI_D2, BOARD_LCD_QSPI_D3, DRAW_BUF_PX * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO),
                        TAG, "spi bus init failed");

    /* esp_lvgl_port registers the transfer-done callback itself. */
    esp_lcd_panel_io_spi_config_t io_cfg = ST77916_PANEL_IO_QSPI_CONFIG(BOARD_LCD_QSPI_CS, NULL, NULL);
    io_cfg.pclk_hz = BOARD_LCD_QSPI_HZ;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST, &io_cfg, &s_io),
        TAG, "qspi panel io failed");

    static const st77916_vendor_config_t vendor = {
        .init_cmds      = board_lcd_init_cmds,
        .init_cmds_size = BOARD_LCD_INIT_CMDS_COUNT,
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = BOARD_LCD_RST,
        .rgb_ele_order  = LCD_BGR ? LCD_RGB_ELEMENT_ORDER_BGR
                                  : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = (void *)&vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st77916(s_io, &dev_cfg, &s_panel),
                        TAG, "st77916 panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),  TAG, "init failed");
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
        .hres          = DIAL_BOARD_LCD_H_RES,
        .vres          = DIAL_BOARD_LCD_V_RES,
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = LCD_MIRROR_X ^ LCD_ROTATE_180,
            .mirror_y = LCD_MIRROR_Y ^ LCD_ROTATE_180,
        },
        .flags = {
            .buff_dma    = true,
            .buff_spiram = false,
            .swap_bytes  = true,   /* the panel takes big-endian RGB565 */
        },
    };
    *out_disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(*out_disp, ESP_FAIL, TAG, "lvgl_port_add_disp failed");

    ESP_RETURN_ON_FALSE(lvgl_port_lock(0), ESP_ERR_TIMEOUT, TAG, "lvgl lock");
    lv_display_add_event_cb(*out_disp, round_area, LV_EVENT_INVALIDATE_AREA, NULL);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "ST77916 %dx%d QSPI up @ %d MHz, %s, mirror_x=%d mirror_y=%d%s",
             DIAL_BOARD_LCD_H_RES, DIAL_BOARD_LCD_V_RES, BOARD_LCD_QSPI_HZ / 1000000,
             LCD_BGR ? "BGR" : "RGB", LCD_MIRROR_X ^ LCD_ROTATE_180,
             LCD_MIRROR_Y ^ LCD_ROTATE_180, LCD_ROTATE_180 ? ", rotated 180" : "");
    return ESP_OK;
}
