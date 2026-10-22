/*
 * Icons the head unit has sent, as 8-bit alpha images LVGL tints.
 *
 * The dial ships no icons of its own. A face names an Ionicon; if the dial
 * does not have it at that size it asks (DIAL_IN_ICON_REQ), and the head
 * unit renders it from the same SVG its own screen uses and uploads it. The
 * cache holds ICON_CACHE_SIZE of them and forgets the least recently shown,
 * which the head unit will simply be asked for again.
 *
 * Uploads arrive on the USB task; everything else runs on the dial task
 * with the display lock held, because LVGL reads the images while drawing.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

esp_err_t icons_init(void);

/* USB task. A new BEGIN abandons an unfinished upload. icons_upload_data()
 * returns true when the upload is complete. */
void icons_upload_begin(uint8_t px, const char *name, size_t name_len);
bool icons_upload_data(uint16_t offset, const uint8_t *data, size_t n);

/* Dial task, display lock held. icons_adopt() moves finished uploads into
 * the cache and returns true if it took any. */
bool icons_adopt(void);
const lv_image_dsc_t *icons_find(const char *name, uint8_t px);
/* The icon on screen, which is never evicted. NULL for none. */
void icons_pin(const lv_image_dsc_t *icon);
