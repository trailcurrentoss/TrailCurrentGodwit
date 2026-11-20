/*
 * The knob and the touch screen, as raw events.
 *
 * THE KNOB IS NOT A QUADRATURE ENCODER. Turning the display housing works
 * an Alps SSCM110100 two-way detector switch: one contact (EC1_A) closes for
 * a moment at each step clockwise, the other (EC1_B) at each step
 * anticlockwise. There are no phases to decode. A step is counted when its
 * contact closes, after a debounce; the contact opening again is nothing.
 * (Waveshare's encoder demo counts the opening instead. Counting the closing
 * reports the step as it is felt.)
 *
 * NO ABSOLUTE POSITION. Only deltas leave this file. Whatever the head unit
 * adjusts, it clamps its own value.
 *
 * THE BUTTON IS THE TOUCH SCREEN. The controller is polled every
 * TOUCH_POLL_MS for the number of fingers on the glass; one or more is
 * "down", none is "up", and a reading is accepted once it has come back the
 * same TOUCH_DEBOUNCE times running. On down, the motor clicks
 * (board_haptic_click) before the head unit is told. Press, hold and
 * "turned while pressed" are the head unit's business, not this file's.
 *
 * THE MOTOR'S TURN TICKS are started and stopped here too (board_haptic_poll,
 * every poll), so the I2C bus has one user.
 */

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_internal.h"
#include "board_pins.h"
#include "dial_board.h"

static const char *TAG = "board.input";

#ifdef CONFIG_WSKNOB_KNOB_INVERT
#  define KNOB_INVERT 1
#else
#  define KNOB_INVERT 0
#endif

#define INPUT_POLL_MS   2
#define KNOB_DEBOUNCE   3      /* samples: 6 ms, as the demo's 2 x 3 ms */
#define TOUCH_EVERY     5      /* polls: the touch is read every 10 ms */
#define TOUCH_DEBOUNCE  2      /* samples: 20 ms */

/* One contact of the detector switch, debounced. */
typedef struct {
    bool closed;
    int  count;    /* consecutive samples disagreeing with `closed` */
} contact_t;

static contact_t s_cw, s_ccw;
static bool      s_touch_down;
static int       s_touch_count;
static int       s_tick;
static unsigned  s_touch_errors;

static dial_board_turn_cb_t   s_turn_cb;
static dial_board_button_cb_t s_button_cb;
static void                  *s_cb_ctx;

void dial_board_set_input_callbacks(dial_board_turn_cb_t turn,
                                    dial_board_button_cb_t button,
                                    void *ctx)
{
    s_cb_ctx    = ctx;
    s_turn_cb   = turn;
    s_button_cb = button;
}

/* Feed one sample; true on the debounced moment the contact closes. */
static bool contact_sample(contact_t *c, bool closed_now)
{
    if (closed_now == c->closed) {
        c->count = 0;
        return false;
    }
    if (++c->count < KNOB_DEBOUNCE) {
        return false;
    }
    c->count  = 0;
    c->closed = closed_now;
    return closed_now;
}

static void poll_knob(void)
{
    int detents = 0;
    if (contact_sample(&s_cw, gpio_get_level(BOARD_KNOB_CW) == 0)) {
        detents += 1;
    }
    if (contact_sample(&s_ccw, gpio_get_level(BOARD_KNOB_CCW) == 0)) {
        detents -= 1;
    }
    if (detents == 0) {
        return;
    }
#if KNOB_INVERT
    detents = -detents;
#endif
#if CONFIG_WSKNOB_KNOB_DEBUG
    ESP_LOGI(TAG, "detents=%+d", detents);
#endif
    if (s_turn_cb) {
        s_turn_cb(detents, s_cb_ctx);
    }
}

static void poll_touch(void)
{
    bool down = false;
    if (board_touch_read(&down) != ESP_OK) {
        /* An unanswered poll -- the controller asleep -- is no finger */
        s_touch_errors++;
        down = false;
    }
    if (down == s_touch_down) {
        s_touch_count = 0;
        return;
    }
    if (++s_touch_count < TOUCH_DEBOUNCE) {
        return;
    }
    s_touch_count = 0;
    s_touch_down  = down;
#if CONFIG_WSKNOB_TOUCH_DEBUG
    ESP_LOGI(TAG, "touch %s (%u unanswered polls so far)", down ? "down" : "up", s_touch_errors);
#endif
    if (down) {
        board_haptic_click();
    }
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
        poll_knob();
        board_haptic_poll();
        if (++s_tick >= TOUCH_EVERY) {
            s_tick = 0;
            poll_touch();
        }
    }
}

esp_err_t board_input_init(void)
{
    /* No internal pull-ups on the contacts: the board has 10 K ones. */
    const gpio_config_t contacts = {
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BOARD_KNOB_CW) | (1ULL << BOARD_KNOB_CCW),
    };
    ESP_RETURN_ON_ERROR(gpio_config(&contacts), TAG, "knob gpio failed");
    /* A contact held closed through boot is not a step. */
    s_cw.closed  = gpio_get_level(BOARD_KNOB_CW) == 0;
    s_ccw.closed = gpio_get_level(BOARD_KNOB_CCW) == 0;

    /* Not fatal: a dial whose touch controller fails to answer still turns. */
    if (board_touch_init() != ESP_OK) {
        ESP_LOGW(TAG, "touch init failed -- the dial has no button");
    }

    /* Above the LVGL task: a redraw must not delay a step or a touch. The
     * stack holds the I2C driver's frames. */
    ESP_RETURN_ON_FALSE(xTaskCreate(input_task, "knob", 4096, NULL, 6, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "knob task");

    ESP_LOGI(TAG, "knob cw=GPIO%d ccw=GPIO%d%s, touch every %d ms",
             BOARD_KNOB_CW, BOARD_KNOB_CCW, KNOB_INVERT ? " (inverted)" : "",
             INPUT_POLL_MS * TOUCH_EVERY);
    return ESP_OK;
}
