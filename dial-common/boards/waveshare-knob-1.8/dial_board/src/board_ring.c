/*
 * The ring, drawn on the display.
 *
 * This board has no LEDs. The head unit still sends its ring frames -- five
 * colours, thirty times a second, carrying the turn direction, a call's
 * comet, the dictation level, alerts and the press and detent flashes -- so
 * they are shown as a band round the outside of the display, and the face
 * is drawn inside it (DIAL_BOARD_FACE_PX).
 *
 * RING MODEL. The head unit draws for five LEDs at -144, -72, 0, 72 and 144
 * degrees, lower-left round over the top to lower-right. The band is drawn
 * in SEGMENTS arcs, each the model blended between the two LEDs either side
 * of its middle; the gap at the bottom blends LED 4 round to LED 0. The
 * frame is premultiplied (an LED that is off is black), so each colour is
 * laid over the screen colour by its own brightness: on the dark theme that
 * is the LED's light, on the light theme a tint of the white rim.
 *
 * COST. A frame redraws only the band -- the sixteen boxes round the edge
 * that cover it, not the display -- and only when it differs from the last.
 * The face in the middle is not touched.
 */

#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"

#include "board_internal.h"
#include "dial_board.h"

static const char *TAG = "board.ring";

#ifdef CONFIG_WSKNOB_SCREEN_RING

#define RING_W     CONFIG_WSKNOB_SCREEN_RING_PX
#define RING_R     (DIAL_BOARD_LCD_V_RES / 2)     /* the band's outer edge */
#define SEGMENTS   36                             /* 10 degrees each */
#define BOXES      16                             /* 22.5 degrees each */
#define FRAME_MS   40                             /* the head unit sends ~30 a second */

#define CX         (DIAL_BOARD_LCD_H_RES / 2)
#define CY         (DIAL_BOARD_LCD_V_RES / 2)

/* Model LED angles, degrees clockwise from 12 o'clock. */
static const int16_t s_model_deg[DIAL_BOARD_RING_LEDS] = { -144, -72, 0, 72, 144 };

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t      s_latest[DIAL_BOARD_RING_LEDS * 3];   /* under s_mux */
static bool         s_latest_new;                          /* under s_mux */
static uint8_t      s_shown[DIAL_BOARD_RING_LEDS * 3];    /* LVGL task only */
static lv_obj_t    *s_scr;
static lv_area_t    s_box[BOXES];                          /* the band, in pieces */

void dial_board_ring_set(const uint8_t rgb[DIAL_BOARD_RING_LEDS * 3])
{
    portENTER_CRITICAL(&s_mux);
    memcpy(s_latest, rgb, sizeof(s_latest));
    s_latest_new = true;
    portEXIT_CRITICAL(&s_mux);
}

/* The model at deg (clockwise from 12 o'clock), blended between the two
 * LEDs either side, into rgb. */
static void sample(const uint8_t *frame, int deg, unsigned rgb[3])
{
#ifdef CONFIG_WSKNOB_ROTATE_180
    deg += 180;
#endif
    deg = ((deg + 180) % 360 + 360) % 360 - 180;    /* -180..179 */
    for (int k = 0; k < DIAL_BOARD_RING_LEDS; k++) {
        const int next = (k + 1) % DIAL_BOARD_RING_LEDS;
        const int a0 = s_model_deg[k];
        /* The last segment wraps through 6 o'clock, 144 -> 216. */
        const int a1 = next ? s_model_deg[next] : s_model_deg[0] + 360;
        int x = deg;
        if (x < a0) {
            x += 360;
        }
        if (x >= a0 && x <= a1) {
            const unsigned t = (unsigned)((x - a0) * 256 / (a1 - a0));
            for (int ch = 0; ch < 3; ch++) {
                rgb[ch] = (frame[k * 3 + ch] * (256u - t) + frame[next * 3 + ch] * t) >> 8;
            }
            return;
        }
    }
    rgb[0] = rgb[1] = rgb[2] = 0;
}

/* An LED's light laid over the screen colour. */
static lv_color_t over(const unsigned rgb[3], lv_color_t bg)
{
    unsigned m = rgb[0];
    if (rgb[1] > m) {
        m = rgb[1];
    }
    if (rgb[2] > m) {
        m = rgb[2];
    }
    const unsigned keep = 255u - m;
    return lv_color_make((uint8_t)LV_MIN(255u, rgb[0] + bg.red * keep / 255u),
                         (uint8_t)LV_MIN(255u, rgb[1] + bg.green * keep / 255u),
                         (uint8_t)LV_MIN(255u, rgb[2] + bg.blue * keep / 255u));
}

/* Screen's DRAW_MAIN_END: after the background, under the face's objects. */
static void draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_color_t bg = lv_obj_get_style_bg_color(s_scr, LV_PART_MAIN);
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = CX;
    d.center.y = CY;
    d.radius   = RING_R;
    d.width    = RING_W;
    const int step = 360 / SEGMENTS;
    for (int i = 0; i < SEGMENTS; i++) {
        const int from = -180 + i * step;         /* from 12 o'clock */
        unsigned rgb[3];
        sample(s_shown, from + step / 2, rgb);
        d.color = over(rgb, bg);
        /* LVGL counts from 3 o'clock; a degree of overlap hides the seams */
        d.start_angle = (lv_value_precise_t)((from - 90 + 720) % 360);
        d.end_angle   = (lv_value_precise_t)((from + step + 1 - 90 + 720) % 360);
        lv_draw_arc(layer, &d);
    }
}

/* LVGL timer: show the newest frame, if it differs from the one on screen. */
static void tick(lv_timer_t *t)
{
    (void)t;
    uint8_t frame[sizeof(s_shown)];
    portENTER_CRITICAL(&s_mux);
    const bool fresh = s_latest_new;
    if (fresh) {
        memcpy(frame, s_latest, sizeof(frame));
        s_latest_new = false;
    }
    portEXIT_CRITICAL(&s_mux);
    if (!fresh || memcmp(frame, s_shown, sizeof(frame)) == 0) {
        return;
    }
    memcpy(s_shown, frame, sizeof(s_shown));
    for (int i = 0; i < BOXES; i++) {
        lv_obj_invalidate_area(s_scr, &s_box[i]);
    }
}

/* The band as BOXES bounding boxes of equal sectors. None straddles an axis
 * (the sectors start at 12 o'clock and 360/BOXES divides 90), so each box is
 * the extent of its four corners, padded a pixel for the antialiasing. */
static void build_boxes(void)
{
    const float ro = RING_R, ri = RING_R - RING_W;
    for (int i = 0; i < BOXES; i++) {
        const float a0 = i * 360.0f / BOXES * (float)M_PI / 180.0f;
        const float a1 = (i + 1) * 360.0f / BOXES * (float)M_PI / 180.0f;
        const float xs[4] = { ro * sinf(a0), ro * sinf(a1), ri * sinf(a0), ri * sinf(a1) };
        const float ys[4] = { -ro * cosf(a0), -ro * cosf(a1), -ri * cosf(a0), -ri * cosf(a1) };
        float x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
        for (int k = 1; k < 4; k++) {
            x0 = LV_MIN(x0, xs[k]);
            x1 = LV_MAX(x1, xs[k]);
            y0 = LV_MIN(y0, ys[k]);
            y1 = LV_MAX(y1, ys[k]);
        }
        s_box[i] = (lv_area_t){
            .x1 = LV_MAX(0, (int32_t)floorf(CX + x0) - 1),
            .y1 = LV_MAX(0, (int32_t)floorf(CY + y0) - 1),
            .x2 = LV_MIN(DIAL_BOARD_LCD_H_RES - 1, (int32_t)ceilf(CX + x1) + 1),
            .y2 = LV_MIN(DIAL_BOARD_LCD_V_RES - 1, (int32_t)ceilf(CY + y1) + 1),
        };
    }
}

esp_err_t board_ring_init(lv_display_t *disp)
{
    build_boxes();
    ESP_RETURN_ON_FALSE(lvgl_port_lock(0), ESP_ERR_TIMEOUT, TAG, "lvgl lock");
    s_scr = lv_display_get_screen_active(disp);
    lv_obj_add_event_cb(s_scr, draw, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_timer_t *timer = lv_timer_create(tick, FRAME_MS, NULL);
    lvgl_port_unlock();
    ESP_RETURN_ON_FALSE(timer, ESP_ERR_NO_MEM, TAG, "timer");
    ESP_LOGI(TAG, "ring drawn on the display, %d px wide; the face gets %d px",
             RING_W, DIAL_BOARD_FACE_PX);
    return ESP_OK;
}

#else /* !CONFIG_WSKNOB_SCREEN_RING */

void dial_board_ring_set(const uint8_t rgb[DIAL_BOARD_RING_LEDS * 3])
{
    (void)rgb;    /* no LEDs, and the on-screen ring is off */
}

esp_err_t board_ring_init(lv_display_t *disp)
{
    (void)disp;
    ESP_LOGI(TAG, "no ring (CONFIG_WSKNOB_SCREEN_RING); the face fills the display");
    return ESP_OK;
}

#endif
