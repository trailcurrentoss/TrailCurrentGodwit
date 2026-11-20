/*
 * The knob: quadrature rotation and the push switch, as raw events.
 *
 * Rotation comes from sampling the phases on a hardware timer and running them
 * through quad_decoder.h, so the CPU being busy redrawing costs nothing. A task
 * passes on the detents every INPUT_POLL_MS. Why a state machine rather than a
 * pulse counter is in that file.
 *
 * NO ABSOLUTE POSITION. The ring has no end stops, so only deltas leave this
 * file. Whatever the head unit adjusts, it clamps its own value.
 *
 * The switch is sampled on the same tick and accepted once it has read the
 * same level DEBOUNCE_SAMPLES times running. Press, hold and "turned while
 * pressed" are the head unit's business, not this file's.
 */

#include <stdatomic.h>

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_internal.h"
#include "board_pins.h"
#include "dial_board.h"
#include "quad_decoder.h"

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

/* Fast enough to see every state at 200 detents a second, well past any spin */
#define SAMPLE_US  500

static uint8_t      s_quad;        /* the decoder's state; the timer ISR's own */
static unsigned     s_flip;        /* XOR on the phases, so rest always reads 11 */
static atomic_int   s_detents;     /* decoded and not yet passed on */
static bool s_btn_down;
static int  s_btn_count;   /* consecutive samples disagreeing with s_btn_down */

static dial_board_turn_cb_t   s_turn_cb;
static dial_board_button_cb_t s_button_cb;
static void                 *s_cb_ctx;

void dial_board_set_input_callbacks(dial_board_turn_cb_t turn,
                                         dial_board_button_cb_t button,
                                         void *ctx)
{
    s_cb_ctx    = ctx;
    s_turn_cb   = turn;
    s_button_cb = button;
}

static bool on_sample(gptimer_handle_t timer, const gptimer_alarm_event_data_t *ev, void *ctx)
{
    (void)timer; (void)ev; (void)ctx;
    const unsigned phases = ((unsigned)gpio_get_level(BOARD_ENC_A) << 1) | (unsigned)gpio_get_level(BOARD_ENC_B);
    const int step = quad_step(&s_quad, phases ^ s_flip);
    if (step != 0) {
        atomic_fetch_add(&s_detents, step);
    }
    return false;
}

static void poll_ring(void)
{
    /* Atomic: the timer's interrupt can run on the other core */
    int detents = atomic_exchange(&s_detents, 0);
    if (detents == 0) {
        return;
    }
#if CONFIG_CROWPANEL_ENCODER_DEBUG
    ESP_LOGI(TAG, "detents=%+d", detents);
#endif
#if ENC_INVERT
    detents = -detents;
#endif
    if (s_turn_cb) {
        s_turn_cb(detents, s_cb_ctx);
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

    /* No internal pull-ups on the phases: this board has external ones. */
    gpio_config_t phases = {
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BOARD_ENC_A) | (1ULL << BOARD_ENC_B),
    };
    ESP_RETURN_ON_ERROR(gpio_config(&phases), TAG, "phase gpio failed");

    /* The decoder counts cycles from its rest state. Encoders of this kind rest
     * with both phases open or both closed, so look which, unless the ring was
     * left between detents, where it does not matter: the first cycle finds it. */
    const int a = gpio_get_level(BOARD_ENC_A), b = gpio_get_level(BOARD_ENC_B);
    s_flip = (a == 0 && b == 0) ? 3 : 0;

    gptimer_handle_t timer = NULL;
    const gptimer_config_t timer_cfg = {
        .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
        .direction     = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&timer_cfg, &timer), TAG, "timer failed");
    const gptimer_event_callbacks_t cbs = { .on_alarm = on_sample };
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(timer, &cbs, NULL), TAG, "timer callback failed");
    const gptimer_alarm_config_t alarm = {
        .alarm_count                = SAMPLE_US,
        .reload_count               = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_RETURN_ON_ERROR(gptimer_set_alarm_action(timer, &alarm), TAG, "timer alarm failed");
    ESP_RETURN_ON_ERROR(gptimer_enable(timer), TAG, "timer enable failed");
    ESP_RETURN_ON_ERROR(gptimer_start(timer), TAG, "timer start failed");

    /* Above the LVGL task: a redraw must not delay a detent or a press. */
    ESP_RETURN_ON_FALSE(xTaskCreate(input_task, "knob", 3072, NULL, 6, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "knob task");

    ESP_LOGI(TAG, "knob A=%d B=%d btn=%d, at rest A=%d B=%d%s",
             BOARD_ENC_A, BOARD_ENC_B, BOARD_ENC_BTN, a, b,
             ENC_INVERT ? ", inverted" : "");
    return ESP_OK;
}
