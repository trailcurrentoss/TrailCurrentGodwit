/*
 * The addressable LED ring: 5 WS2812s around the panel.
 *
 * Driven by the RMT peripheral with ESP-IDF's own bytes encoder: WS2812 bits
 * are one high/low pulse pair each, which is exactly what a bytes encoder
 * emits, so no extra component is needed. Colour order on the wire is GRB.
 *
 * RING MODEL VS. PHYSICAL LEDS
 *
 * The head unit draws its ring patterns for five LEDs at -144, -72, 0, 72
 * and 144 degrees: lower-left round over the top to lower-right. This
 * board's LEDs are at 4, 1, 11, just shy of 9 and 6 o'clock instead
 * (BOARD_WS2812_ANGLES). Each physical LED shows the model blended between
 * the two model LEDs either side of its own angle, so a pattern that points
 * somewhere on the ring still points there here. The 6 o'clock LED sits in
 * the gap at the bottom of the model and shows the average of its two ends.
 *
 * With CROWPANEL_ROTATE_180 every LED moves round by 180 degrees relative
 * to the face, which the model is drawn against. The 6 o'clock LED then
 * sits at 12 and shows the top model LED exactly.
 */

#include <string.h>

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board_internal.h"
#include "board_pins.h"
#include "crowpanel_board.h"

static const char *TAG = "board.leds";

/* 10 MHz: one tick is 100 ns. WS2812: a 0 is ~0.3 us high then ~0.9 us low,
 * a 1 is ~0.9 us high then ~0.3 us low; >50 us low latches the frame. */
#define LED_RMT_HZ 10000000

/* Model LED angles, degrees clockwise from 12 o'clock. */
static const int16_t s_model_deg[CROWPANEL_RING_LEDS] = { -144, -72, 0, 72, 144 };

/* For each physical LED: the two model LEDs it sits between, and how far
 * along from the first to the second (0-256). */
typedef struct {
    uint8_t  a, b;
    uint16_t t;
} led_mix_t;

static led_mix_t            s_mix[BOARD_WS2812_COUNT];
static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_enc;
static uint8_t              s_grb[BOARD_WS2812_COUNT * 3];
static uint8_t              s_sent[BOARD_WS2812_COUNT * 3];
static bool                 s_ready;
static bool                 s_sent_valid;               /* force first write */
static SemaphoreHandle_t    s_lock;    /* frames come from USB and app tasks */

static void build_mix(void)
{
    static const int16_t phys[BOARD_WS2812_COUNT] = BOARD_WS2812_ANGLES;
    for (int i = 0; i < BOARD_WS2812_COUNT; i++) {
        s_mix[i] = (led_mix_t){ 0, 0, 0 };
        for (int k = 0; k < CROWPANEL_RING_LEDS; k++) {
            const int next = (k + 1) % CROWPANEL_RING_LEDS;
            const int a0 = s_model_deg[k];
            /* The last segment wraps through 6 o'clock, 144 -> 216. */
            const int a1 = next ? s_model_deg[next] : s_model_deg[0] + 360;
            int x = phys[i];
#ifdef CONFIG_CROWPANEL_ROTATE_180
            x = x >= 0 ? x - 180 : x + 180;   /* stays within -180..180 */
#endif
            if (x < a0) {
                x += 360;
            }
            if (x >= a0 && x <= a1) {
                s_mix[i] = (led_mix_t){ (uint8_t)k, (uint8_t)next,
                                        (uint16_t)((x - a0) * 256 / (a1 - a0)) };
                break;
            }
        }
    }
}

/* Scale one channel by the brightness ceiling. */
static uint8_t scale(unsigned v)
{
    return (uint8_t)(v * (unsigned)CONFIG_CROWPANEL_LEDS_MAX_BRIGHTNESS / 100u);
}

/* Send s_grb unless it is what the ring already shows. Call with s_lock. */
static void send(void)
{
    if (s_sent_valid && memcmp(s_grb, s_sent, sizeof(s_grb)) == 0) {
        return;
    }
    const rmt_transmit_config_t tx = { .loop_count = 0 };
    if (rmt_transmit(s_chan, s_enc, s_grb, sizeof(s_grb), &tx) != ESP_OK) {
        ESP_LOGW(TAG, "LED write failed");
        s_sent_valid = false;              /* retry on the next call */
        return;
    }
    /* The RMT reads s_grb in the background; wait so the next frame cannot
     * overwrite it mid-transmission. 5 LEDs take ~150 us. */
    rmt_tx_wait_all_done(s_chan, 10);
    memcpy(s_sent, s_grb, sizeof(s_grb));
    s_sent_valid = true;
}

void crowpanel_board_ring_set(const uint8_t rgb[CROWPANEL_RING_LEDS * 3])
{
    if (!s_ready) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < BOARD_WS2812_COUNT; i++) {
        const led_mix_t m = s_mix[i];
        unsigned c[3];
        for (int ch = 0; ch < 3; ch++) {
            c[ch] = (rgb[m.a * 3 + ch] * (256u - m.t) + rgb[m.b * 3 + ch] * m.t) >> 8;
        }
        s_grb[i * 3 + 0] = scale(c[1]);   /* GRB on the wire */
        s_grb[i * 3 + 1] = scale(c[0]);
        s_grb[i * 3 + 2] = scale(c[2]);
    }
    send();
    xSemaphoreGive(s_lock);
}

esp_err_t board_leds_init(void)
{
    const rmt_tx_channel_config_t ch = {
        .gpio_num = BOARD_WS2812_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RMT_HZ,
        .mem_block_symbols = 48,
        .trans_queue_depth = 2,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&ch, &s_chan), TAG, "rmt channel");

    const rmt_bytes_encoder_config_t enc = {
        .bit0 = { .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9 },
        .bit1 = { .level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3 },
        .flags.msb_first = 1,
    };
    ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&enc, &s_enc), TAG, "encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_chan), TAG, "rmt enable");

    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "lock");

    build_mix();
    s_ready = true;
    ESP_LOGI(TAG, "%d LEDs on GPIO%d, max %d%%", BOARD_WS2812_COUNT,
             BOARD_WS2812_GPIO, CONFIG_CROWPANEL_LEDS_MAX_BRIGHTNESS);

    static const uint8_t off[CROWPANEL_RING_LEDS * 3];
    crowpanel_board_ring_set(off);          /* known state, not power-on noise */
    return ESP_OK;
}
