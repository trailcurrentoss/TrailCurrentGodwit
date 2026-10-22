/*
 * The Godwit dial protocol: what a dial and the head unit say to each other
 * over the dial's vendor-defined HID interface (hidraw on the head unit).
 *
 * The head unit keeps its own copy of these numbers in
 * hmi/src/dialprotocol.h. Change both together, and bump
 * DIAL_PROTO_VERSION when a change is not backwards compatible.
 * dial-common/docs/protocol.md describes the exchange in prose.
 *
 * Every report is a report ID plus DIAL_REPORT_LEN payload bytes, zero
 * padded, except DIAL_IN_EVENT, which is short so a turn costs little.
 * Multi-byte numbers are little-endian. Text is UTF-8, NUL padded, and is
 * cut by the sender on a character boundary.
 */
#pragma once

#include <stdint.h>

#define DIAL_PROTO_VERSION   1
#define DIAL_REPORT_LEN      63

/* ---- Dial -> head unit: input reports ---------------------------------- */

/* A knob event: [kind, arg]. */
#define DIAL_IN_EVENT        0x01
#define DIAL_IN_EVENT_LEN    2
enum {
    DIAL_EV_DOWN = 1,        /* push switch closed; arg 0 */
    DIAL_EV_UP   = 2,        /* push switch opened; arg 0 */
    DIAL_EV_TURN = 3,        /* arg: signed detents, clockwise positive */
};

/* "Send me this icon": [size px, name...]. The dial asks when a face names
 * an icon it does not have at that size, and again a second later if none
 * has arrived. */
#define DIAL_IN_ICON_REQ     0x02

/* ---- Feature report ----------------------------------------------------- */

/* Read by the head unit when it opens the dial:
 * [DIAL_PROTO_VERSION, side (0 left, 1 right), firmware version...]. */
#define DIAL_FEATURE_INFO    0x03

/* ---- Head unit -> dial: output reports --------------------------------- */

/* One ring frame: 5 x [r, g, b], already scaled for brightness, in the ring
 * model -- LED 0 lower-left, round over the top, to LED 4 lower-right. The
 * head unit sends these continuously (~30 per second); they double as its
 * heartbeat. */
#define DIAL_OUT_RING        0x10

/* One text field of the next face: [field, utf8...]. Takes effect on the
 * next DIAL_OUT_FACE. A field not sent keeps its last value. */
#define DIAL_OUT_TEXT        0x11
#define DIAL_TEXT_MAX        (DIAL_REPORT_LEN - 1)
enum {
    DIAL_FIELD_LABEL = 0,    /* small caps line at the top (sent upper-cased) */
    DIAL_FIELD_VALUE,        /* the big value */
    DIAL_FIELD_UNIT,         /* after the value, on its baseline */
    DIAL_FIELD_SUB,          /* line under the value */
    DIAL_FIELD_ICON,         /* Ionicons name, e.g. "navigate-outline" */
    DIAL_FIELD_COUNT,
};

/* Face style, and commit: the dial redraws with the text sent so far. */
#define DIAL_OUT_FACE        0x12
#define DIAL_FACE_DARK       0x01    /* dark theme */
#define DIAL_FACE_SMALL      0x02    /* value in bold (a name, not a number) */
#define DIAL_PAGER_NONE      0xFF
enum {
    DIAL_COLOR_TX = 0,       /* the theme's text colour */
    DIAL_COLOR_ACC,
    DIAL_COLOR_HEAT,
    DIAL_COLOR_COOL,
    DIAL_COLOR_WARN,
};
typedef struct __attribute__((packed)) {
    uint8_t flags;           /* DIAL_FACE_* */
    uint8_t color;           /* DIAL_COLOR_*: value and icon colour */
    uint8_t value_px;        /* value font size */
    uint8_t icon_px;         /* icon size */
    uint8_t pager_count;     /* page dots under the text; 0 for none */
    uint8_t pager_on;        /* index of the current page, or DIAL_PAGER_NONE */
    uint8_t backlight;       /* percent */
} dial_face_t;

/* An icon, in answer to DIAL_IN_ICON_REQ. BEGIN: [size px, name...]. Then
 * DATA reports carry size*size bytes of 8-bit alpha, row by row:
 * [offset lo, offset hi, count, bytes...]. The icon is complete when the
 * last byte arrives. */
#define DIAL_OUT_ICON_BEGIN  0x13
#define DIAL_OUT_ICON_DATA   0x14
#define DIAL_ICON_CHUNK      (DIAL_REPORT_LEN - 3)
#define DIAL_ICON_MAX_PX     96

/* A command: [cmd]. */
#define DIAL_OUT_CONTROL     0x15
enum {
    DIAL_CMD_BOOTLOADER = 1, /* reboot into the ROM loader, for flashing */
};
