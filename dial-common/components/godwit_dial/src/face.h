/*
 * The round display: what the head unit's KnobFace.qml shows, drawn with
 * LVGL at the same sizes, fonts and colours -- scaled up from KnobFace.qml's
 * 240 px design to DIAL_BOARD_FACE_PX on a board with a bigger display.
 *
 * Top to bottom, centred: the label, an icon, the value with its unit on
 * the same baseline, the sub line, and the page dots. Behind them, round the
 * edge, the face's scale (DIAL_SCALE_*). All calls run with the display lock
 * held.
 */
#pragma once

#include "dial_protocol.h"
#include "lvgl.h"

typedef struct {
    char        text[DIAL_FIELD_COUNT][DIAL_TEXT_MAX + 1];
    dial_face_t style;
} face_t;

void face_init(void);

/* icon is the image for text[DIAL_FIELD_ICON], face_icon_host_px() across,
 * or NULL if there is none yet; the space for it is kept either way, so
 * nothing moves when it arrives. */
void face_show(const face_t *f, const lv_image_dsc_t *icon);

/* The size to ask the head unit for an icon the face shows at icon_px: the
 * scaled size, capped at what the protocol carries. The face stretches it
 * the rest of the way, which on a 240 px display is not at all. */
uint8_t face_icon_host_px(uint8_t icon_px);
