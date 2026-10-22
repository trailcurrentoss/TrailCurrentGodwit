/*
 * The knob: quadrature rotation and the push switch, as raw events.
 *
 * Rotation is decoded in hardware by PCNT rather than a GPIO interrupt per
 * edge, so no step is lost while the CPU is busy redrawing. A task drains the
 * counter every INPUT_POLL_MS and reports a detent each time the count has
 * moved a whole detent from the last one reported, either way. Contact bounce
 * swings the count back and forth by an edge or two, which never adds up to a
 * detent, and a real click back is a whole detent, so a reversal still takes
 * effect on the very next click.
 *
 * The remainder is never thrown away. Dropping it whenever a poll counted
 * backwards (as this file once did) let bounce shift the reported detents off
 * the mechanical ones, after which bounce at a click produced detents the
 * wrong way: 47 of 286 reports in one fast-spin recording.
 *
 * NO ABSOLUTE POSITION. The ring has no end stops, so only deltas leave this
 * file. Whatever the head unit adjusts, it clamps its own value.
 *
 * The switch is sampled on the same tick and accepted once it has read the
 * same level DEBOUNCE_SAMPLES times running. Press, hold and "turned while
 * pressed" are the head unit's business, not this file's.
 */

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_internal.h"
#include "board_pins.h"
#include "crowpanel_board.h"

static const char *TAG = "board.knob";

#ifdef CONFIG_CROWPANEL_ENCODER_INVERT
#  define ENC_INVERT 1
#else
#  define ENC_INVERT 0
#endif

/* 2 ms polls with 3 matching samples: a 6 ms debounce, just over the
 * EC3501's 5 ms worst-case chatter. */
#define INPUT_POLL_MS     2
#define DEBOUNCE_SAMPLES  3

/* The counter is drained every poll, so these only need to cover one. */
#define PCNT_HIGH_LIMIT  1000
#define PCNT_LOW_LIMIT  -1000

static pcnt_unit_handle_t s_pcnt;

static int  s_residue;     /* counts since the last detent, within one detent */
static bool s_btn_down;
static int  s_btn_count;   /* consecutive samples disagreeing with s_btn_down */

static crowpanel_turn_cb_t   s_turn_cb;
static crowpanel_button_cb_t s_button_cb;
static void                 *s_cb_ctx;

void crowpanel_board_set_input_callbacks(crowpanel_turn_cb_t turn,
                                         crowpanel_button_cb_t button,
                                         void *ctx)
{
    s_cb_ctx    = ctx;
    s_turn_cb   = turn;
    s_button_cb = button;
}

static void poll_ring(void)
{
    int raw = 0;
    if (pcnt_unit_get_count(s_pcnt, &raw) != ESP_OK || raw == 0) {
        return;
    }
    pcnt_unit_clear_count(s_pcnt);

    s_residue += raw;

    const int per = CONFIG_CROWPANEL_ENCODER_STEPS_PER_DETENT;
    int detents = s_residue / per;   /* toward zero: hysteresis of a detent */
    s_residue -= detents * per;

#if CONFIG_CROWPANEL_ENCODER_DEBUG
    ESP_LOGI(TAG, "raw=%+d residue=%+d detents=%+d", raw, s_residue, detents);
#endif

    if (detents != 0) {
#if ENC_INVERT
        detents = -detents;
#endif
        if (s_turn_cb) {
            s_turn_cb(detents, s_cb_ctx);
        }
    }
}

static void poll_button(void)
{
    const bool down = gpio_get_level(BOARD_ENC_BTN) == 0;
    if (down == s_btn_down) {
        s_btn_count = 0;
        return;
    }
    if (++s_btn_count < DEBOUNCE_SAMPLES) {
        return;
    }
    s_btn_count = 0;
    s_btn_down  = down;
    if (s_button_cb) {
        s_button_cb(down, s_cb_ctx);
    }
}

static void input_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(INPUT_POLL_MS));
        poll_ring();
        poll_button();
    }
}

esp_err_t board_input_init(void)
{
    gpio_config_t btn = {
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << BOARD_ENC_BTN,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&btn), TAG, "button gpio failed");
    /* A knob held down through boot is not a press. */
    s_btn_down = gpio_get_level(BOARD_ENC_BTN) == 0;

    pcnt_unit_config_t unit_cfg = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit  = PCNT_LOW_LIMIT,
    };
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_cfg, &s_pcnt), TAG, "pcnt unit failed");

    /* Kills electrical noise. Mechanical bounce nets to zero under full
     * quadrature decoding, so it needs no filter. */
    pcnt_glitch_filter_config_t filt = { .max_glitch_ns = 1000 };
    ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(s_pcnt, &filt),
                        TAG, "glitch filter failed");

    pcnt_chan_config_t ch_a_cfg = { .edge_gpio_num = BOARD_ENC_A,
                                    .level_gpio_num = BOARD_ENC_B };
    pcnt_chan_config_t ch_b_cfg = { .edge_gpio_num = BOARD_ENC_B,
                                    .level_gpio_num = BOARD_ENC_A };
    pcnt_channel_handle_t ch_a = NULL, ch_b = NULL;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_pcnt, &ch_a_cfg, &ch_a), TAG, "ch a failed");
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_pcnt, &ch_b_cfg, &ch_b), TAG, "ch b failed");

    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(ch_a,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE),
        TAG, "ch a edge failed");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(ch_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE),
        TAG, "ch a level failed");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(ch_b,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE),
        TAG, "ch b edge failed");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(ch_b,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE),
        TAG, "ch b level failed");

    /* No internal pull-ups on the phases: this board has external ones. */

    ESP_RETURN_ON_ERROR(pcnt_unit_enable(s_pcnt),      TAG, "pcnt enable failed");
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(s_pcnt), TAG, "pcnt clear failed");
    ESP_RETURN_ON_ERROR(pcnt_unit_start(s_pcnt),       TAG, "pcnt start failed");

    /* Above the LVGL task: a redraw must not delay a detent or a press. */
    ESP_RETURN_ON_FALSE(xTaskCreate(input_task, "knob", 3072, NULL, 6, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "knob task");

    ESP_LOGI(TAG, "knob A=%d B=%d btn=%d, %d counts/detent%s",
             BOARD_ENC_A, BOARD_ENC_B, BOARD_ENC_BTN,
             CONFIG_CROWPANEL_ENCODER_STEPS_PER_DETENT,
             ENC_INVERT ? ", inverted" : "");
    return ESP_OK;
}
