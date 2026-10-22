/*
 * Icon cache. See icons.h.
 */

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "dial_protocol.h"
#include "icons.h"

static const char *TAG = "icons";

/* An 84 px icon is 7 KB, so a full cache is under 350 KB of PSRAM. The
 * head unit uses about 45 distinct icons in all, at two sizes. */
#define ICON_CACHE_SIZE 48

typedef struct {
    char     name[DIAL_TEXT_MAX + 1];
    uint8_t  px;
    uint8_t *data;
} upload_t;

typedef struct {
    upload_t       img;
    lv_image_dsc_t dsc;
    uint32_t       used;     /* last-shown stamp, for eviction */
} icon_t;

static icon_t                s_cache[ICON_CACHE_SIZE];
static uint32_t              s_stamp;
static const lv_image_dsc_t *s_pinned;

/* USB task only. */
static upload_t s_up;
static size_t   s_up_len;

/* Finished uploads, USB task -> dial task. */
static QueueHandle_t s_done;

esp_err_t icons_init(void)
{
    s_done = xQueueCreate(4, sizeof(upload_t));
    ESP_RETURN_ON_FALSE(s_done, ESP_ERR_NO_MEM, TAG, "queue");
    return ESP_OK;
}

void icons_upload_begin(uint8_t px, const char *name, size_t name_len)
{
    free(s_up.data);
    s_up.data = NULL;
    if (px == 0 || px > DIAL_ICON_MAX_PX) {
        ESP_LOGW(TAG, "icon size %u refused", px);
        return;
    }
    if (name_len > DIAL_TEXT_MAX) {
        name_len = DIAL_TEXT_MAX;
    }
    memcpy(s_up.name, name, name_len);
    s_up.name[name_len] = '\0';
    s_up.px  = px;
    s_up_len = (size_t)px * px;
    s_up.data = heap_caps_malloc(s_up_len, MALLOC_CAP_SPIRAM);
    if (!s_up.data) {
        ESP_LOGW(TAG, "no memory for '%s'", s_up.name);
    }
}

bool icons_upload_data(uint16_t offset, const uint8_t *data, size_t n)
{
    if (!s_up.data || offset + n > s_up_len) {
        return false;
    }
    memcpy(s_up.data + offset, data, n);
    if (offset + n < s_up_len) {
        return false;
    }
    /* The head unit sends the chunks in order, so the last byte arriving
     * means the icon is whole. */
    if (xQueueSend(s_done, &s_up, 0) != pdTRUE) {
        free(s_up.data);          /* the dial will ask again */
    }
    s_up.data = NULL;
    return true;
}

static void evict(icon_t *slot)
{
    lv_image_cache_drop(&slot->dsc);
    free(slot->img.data);
    memset(slot, 0, sizeof(*slot));
}

static icon_t *lookup(const char *name, uint8_t px)
{
    for (int i = 0; i < ICON_CACHE_SIZE; i++) {
        icon_t *c = &s_cache[i];
        if (c->img.data && c->img.px == px && strcmp(c->img.name, name) == 0) {
            return c;
        }
    }
    return NULL;
}

bool icons_adopt(void)
{
    bool took = false;
    upload_t u;
    while (xQueueReceive(s_done, &u, 0) == pdTRUE) {
        if (lookup(u.name, u.px)) {
            free(u.data);           /* asked twice; keep the one on screen */
            continue;
        }
        icon_t *slot = NULL;
        for (int i = 0; i < ICON_CACHE_SIZE; i++) {
            icon_t *c = &s_cache[i];
            if (!c->img.data) {
                slot = c;
                break;
            }
            if (&c->dsc != s_pinned && (!slot || c->used < slot->used)) {
                slot = c;
            }
        }
        if (slot->img.data) {
            evict(slot);
        }
        slot->img = u;
        slot->used = ++s_stamp;
        slot->dsc = (lv_image_dsc_t){
            .header = {
                .magic  = LV_IMAGE_HEADER_MAGIC,
                .cf     = LV_COLOR_FORMAT_A8,
                .w      = u.px,
                .h      = u.px,
                .stride = u.px,
            },
            .data_size = (uint32_t)u.px * u.px,
            .data      = u.data,
        };
        ESP_LOGD(TAG, "got '%s' at %u px", u.name, u.px);
        took = true;
    }
    return took;
}

const lv_image_dsc_t *icons_find(const char *name, uint8_t px)
{
    icon_t *c = lookup(name, px);
    if (!c) {
        return NULL;
    }
    c->used = ++s_stamp;
    return &c->dsc;
}

void icons_pin(const lv_image_dsc_t *icon)
{
    s_pinned = icon;
}
