/*
 * The round display: what the head unit's KnobFace.qml shows, drawn with
 * LVGL at the same sizes, fonts and colours.
 *
 * Top to bottom, centred: the label, an icon, the value with its unit on
 * the same baseline, the sub line, and the page dots. All calls run with the
 * display lock held.
 */
#pragma once

#include "dial_protocol.h"
#include "lvgl.h"

typedef struct {
    char        text[DIAL_FIELD_COUNT][DIAL_TEXT_MAX + 1];
    dial_face_t style;
} face_t;

void face_init(void);

/* icon is the image for text[DIAL_FIELD_ICON], or NULL if there is none yet;
 * the space for it is kept either way, so nothing moves when it arrives. */
void face_show(const face_t *f, const lv_image_dsc_t *icon);
