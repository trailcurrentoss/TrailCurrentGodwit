/*
 * A Godwit dial: one of the head unit's two encoder modules.
 *
 * The dial holds no menus and no state of its own. The head unit decides
 * everything, and this firmware is its hands and its two small screens:
 *
 *   knob    -> DIAL_IN_EVENT: detents, switch down, switch up. Press, hold
 *              and turn-while-pressed are decided by the head unit's Knobs
 *              class, so the dials, the keyboard and the simulator share one
 *              set of timings.
 *   ring    <- DIAL_OUT_RING: ~30 frames a second, shown as they arrive.
 *   display <- DIAL_OUT_TEXT + DIAL_OUT_FACE: drawn on commit. Icons the face
 *              names are asked for (DIAL_IN_ICON_REQ) and cached.
 *
 * The ring frames double as the head unit's heartbeat. When none has come
 * for CONFIG_GODWIT_DIAL_HOST_TIMEOUT_MS -- the HMI is not running, or the
 * cable is out -- the dial shows a standby face of its own and breathes the
 * ring, and goes back to the head unit's last face when the frames resume.
 *
 * Threads:
 *   TinyUSB task  on_report(): ring frames straight to the LEDs, text and
 *                 icon bytes into staging, commits handed to the dial task
 *   knob task     on_turn() / on_button(): queued as input reports
 *   dial task     everything LVGL, under the display lock
 */

#include <ctype.h>
#include <math.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "crowpanel_board.h"
#include "dial_protocol.h"
#include "dial_usb.h"
#include "face.h"
#include "godwit_dial.h"
#include "icons.h"

static const char *TAG = "dial";

#if CONFIG_GODWIT_DIAL_SIDE_RIGHT
#  define DIAL_SIDE       1
#  define DIAL_SIDE_NAME  "Right"
#else
#  define DIAL_SIDE       0
#  define DIAL_SIDE_NAME  "Left"
#endif

#define HOST_TIMEOUT_US   ((int64_t)CONFIG_GODWIT_DIAL_HOST_TIMEOUT_MS * 1000)
#define ICON_RETRY_US     1000000
#define STANDBY_FRAME_MS  40
#define STANDBY_BACKLIGHT 60
#define MIN_BACKLIGHT     5      /* a dial asked for 0 % must not look dead */

static TaskHandle_t s_task;

/* ---- Head unit -> dial (TinyUSB task) ---------------------------------- */

static face_t           s_staged;          /* TinyUSB task only */
static portMUX_TYPE     s_mux = portMUX_INITIALIZER_UNLOCKED;
static face_t           s_committed;       /* under s_mux */
static bool             s_committed_new;   /* under s_mux */
static volatile int64_t s_last_host_us;

static void copy_text(char *dst, const uint8_t *src, size_t len)
{
    if (len > DIAL_TEXT_MAX) {
        len = DIAL_TEXT_MAX;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';      /* the report is NUL padded; strlen finds the end */
}

static void on_report(uint8_t id, const uint8_t *d, size_t len, void *ctx)
{
    (void)ctx;
    s_last_host_us = esp_timer_get_time();

    switch (id) {
    case DIAL_OUT_RING:
        if (len >= CROWPANEL_RING_LEDS * 3) {
            crowpanel_board_ring_set(d);
        }
        break;

    case DIAL_OUT_TEXT:
        if (len >= 1 && d[0] < DIAL_FIELD_COUNT) {
            copy_text(s_staged.text[d[0]], d + 1, len - 1);
        }
        break;

    case DIAL_OUT_FACE:
        if (len >= sizeof(dial_face_t)) {
            memcpy(&s_staged.style, d, sizeof(dial_face_t));
            portENTER_CRITICAL(&s_mux);
            s_committed     = s_staged;
            s_committed_new = true;
            portEXIT_CRITICAL(&s_mux);
            xTaskNotifyGive(s_task);
        }
        break;

    case DIAL_OUT_ICON_BEGIN:
        if (len >= 2) {
            icons_upload_begin(d[0], (const char *)d + 1, strnlen((const char *)d + 1, len - 1));
        }
        break;

    case DIAL_OUT_ICON_DATA:
        if (len >= 3 && d[2] <= len - 3 &&
            icons_upload_data((uint16_t)(d[0] | d[1] << 8), d + 3, d[2])) {
            xTaskNotifyGive(s_task);
        }
        break;

    case DIAL_OUT_CONTROL:
        if (len >= 1 && d[0] == DIAL_CMD_BOOTLOADER) {
            dial_usb_request_bootloader();
        }
        break;

    default:
        break;
    }
}

/* ---- Knob -> head unit (knob task) ------------------------------------- */

static void send_event(uint8_t kind, int8_t arg)
{
    const uint8_t ev[DIAL_IN_EVENT_LEN] = { kind, (uint8_t)arg };
    dial_usb_send(DIAL_IN_EVENT, ev, sizeof(ev));   /* no host: nobody to tell */
}

static void on_turn(int detents, void *ctx)
{
    (void)ctx;
    while (detents) {
        const int step = detents > 127 ? 127 : detents < -127 ? -127 : detents;
        send_event(DIAL_EV_TURN, (int8_t)step);
        detents -= step;
    }
}

static void on_button(bool down, void *ctx)
{
    (void)ctx;
    send_event(down ? DIAL_EV_DOWN : DIAL_EV_UP, 0);
}

/* ---- The dial task ------------------------------------------------------ */

static face_t  s_host_face;    /* the head unit's latest face */
static bool    s_have_face;
static bool    s_standby = true;
static bool    s_standby_mounted;
static bool    s_dirty;
static bool    s_icon_missing;
static char    s_req_name[DIAL_TEXT_MAX + 1];
static uint8_t s_req_px;
static int64_t s_req_us;

static void request_icon(const char *name, uint8_t px)
{
    const int64_t now = esp_timer_get_time();
    if (px == s_req_px && strcmp(name, s_req_name) == 0 && now - s_req_us < ICON_RETRY_US) {
        return;
    }
    uint8_t req[DIAL_REPORT_LEN] = { px };
    strlcpy((char *)req + 1, name, sizeof(req) - 1);
    if (dial_usb_send(DIAL_IN_ICON_REQ, req, sizeof(req)) == ESP_OK) {
        strlcpy(s_req_name, name, sizeof(s_req_name));
        s_req_px = px;
        s_req_us = now;
    }
}

static void standby_face(face_t *f, bool mounted)
{
    memset(f, 0, sizeof(*f));
    strlcpy(f->text[DIAL_FIELD_LABEL], DIAL_SIDE_NAME " dial", DIAL_TEXT_MAX);
    for (char *p = f->text[DIAL_FIELD_LABEL]; *p; p++) {
        *p = (char)toupper((unsigned char)*p);
    }
    strlcpy(f->text[DIAL_FIELD_VALUE], "Godwit", DIAL_TEXT_MAX);
    strlcpy(f->text[DIAL_FIELD_SUB], mounted ? "Waiting for head unit" : "Not connected",
            DIAL_TEXT_MAX);
    f->style = (dial_face_t){
        .flags       = DIAL_FACE_DARK | DIAL_FACE_SMALL,
        .color       = DIAL_COLOR_ACC,
        .value_px    = 38,
        .icon_px     = 84,
        .pager_on    = DIAL_PAGER_NONE,
        .backlight   = STANDBY_BACKLIGHT,
    };
}

/* Display lock held. */
static void show(const face_t *f)
{
    const char *name = f->text[DIAL_FIELD_ICON];
    const lv_image_dsc_t *icon = NULL;
    s_icon_missing = false;
    if (name[0]) {
        icon = icons_find(name, f->style.icon_px);
        if (!icon) {
            s_icon_missing = true;
            request_icon(name, f->style.icon_px);
        }
    }
    icons_pin(icon);
    face_show(f, icon);

    uint8_t bl = f->style.backlight;
    crowpanel_board_backlight_set(bl < MIN_BACKLIGHT ? MIN_BACKLIGHT : bl > 100 ? 100 : bl);
}

/* Slow breath of the accent green while the head unit is away. */
static void standby_ring(int64_t now_us)
{
    const float t = (float)now_us / 1e6f;
    const float level = 0.05f + 0.20f * (0.5f + 0.5f * sinf(t * 2.0f * (float)M_PI / 4.0f));
    uint8_t rgb[CROWPANEL_RING_LEDS * 3];
    for (int k = 0; k < CROWPANEL_RING_LEDS; k++) {
        rgb[k * 3 + 0] = (uint8_t)(0x7B * level);
        rgb[k * 3 + 1] = (uint8_t)(0xC9 * level);
        rgb[k * 3 + 2] = (uint8_t)(0x6A * level);
    }
    crowpanel_board_ring_set(rgb);
}

static void dial_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(STANDBY_FRAME_MS));
        const int64_t now = esp_timer_get_time();

        portENTER_CRITICAL(&s_mux);
        if (s_committed_new) {
            s_host_face     = s_committed;
            s_committed_new = false;
            s_have_face     = true;
            s_dirty         = true;
        }
        portEXIT_CRITICAL(&s_mux);

        const bool mounted = dial_usb_mounted();
        const int64_t last = s_last_host_us;
        const bool standby = !mounted || !last || now - last > HOST_TIMEOUT_US;
        if (standby != s_standby) {
            ESP_LOGI(TAG, "%s", standby ? "head unit gone -- standby" : "head unit here");
            s_standby = standby;
            s_dirty = true;
        }
        if (s_standby && mounted != s_standby_mounted) {
            s_standby_mounted = mounted;
            s_dirty = true;
        }

        if (crowpanel_board_lock(100)) {
            if (icons_adopt() && s_icon_missing) {
                s_dirty = true;
            }
            if (s_dirty) {
                if (s_standby || !s_have_face) {
                    face_t f;
                    standby_face(&f, mounted);
                    show(&f);
                } else {
                    show(&s_host_face);
                }
                s_dirty = false;
            } else if (s_icon_missing && !s_standby) {
                request_icon(s_host_face.text[DIAL_FIELD_ICON], s_host_face.style.icon_px);
            }
            crowpanel_board_unlock();
        }

        if (s_standby) {
            standby_ring(now);
        }
    }
}

/* ------------------------------------------------------------------------ */

void godwit_dial_start(void)
{
    ESP_LOGI(TAG, "Godwit dial, %s side", DIAL_SIDE_NAME);

    ESP_ERROR_CHECK(crowpanel_board_init());
    ESP_ERROR_CHECK(icons_init());

    if (crowpanel_board_lock(0)) {
        face_init();
        face_t f;
        standby_face(&f, false);
        show(&f);
        crowpanel_board_unlock();
    }

    /* Before USB: its callbacks notify this task. */
    xTaskCreate(dial_task, "dial", 6144, NULL, 3, &s_task);
    configASSERT(s_task);

    crowpanel_board_set_input_callbacks(on_turn, on_button, NULL);

    uint8_t info[DIAL_REPORT_LEN] = { DIAL_PROTO_VERSION, DIAL_SIDE };
    strlcpy((char *)info + 2, esp_app_get_description()->version, sizeof(info) - 2);
    const dial_usb_config_t usb = {
        .product   = "Godwit Dial " DIAL_SIDE_NAME,
        .info      = info,
        .info_len  = sizeof(info),
        .on_report = on_report,
    };
    ESP_ERROR_CHECK(dial_usb_init(&usb));

    /* The standby face is drawn; now it can be seen. */
    ESP_ERROR_CHECK(crowpanel_board_backlight_set(STANDBY_BACKLIGHT));
}
