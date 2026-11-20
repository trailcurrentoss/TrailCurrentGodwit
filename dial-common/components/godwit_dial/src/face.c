/*
 * The round display. See face.h.
 *
 * Text is drawn with LVGL's TinyTTF from the head unit's own Inter TTFs
 * (embedded from hmi/assets/fonts), so the dial can show any size the head
 * unit asks for and any character a contact name or network name contains.
 * Each weight and size is rasterised on first use and kept.
 *
 * Measurements mirror hmi/qml/knob/KnobFace.qml. Change them together. They
 * are in the 240 px design the first dial had; FACE_K scales them to the
 * board's display.
 */

#include <math.h>
#include <string.h>

#include "esp_log.h"

#include "dial_board.h"
#include "face.h"

static const char *TAG = "face";

extern const uint8_t inter_regular_start[]  asm("_binary_Inter_Regular_ttf_start");
extern const uint8_t inter_regular_end[]    asm("_binary_Inter_Regular_ttf_end");
extern const uint8_t inter_semibold_start[] asm("_binary_Inter_SemiBold_ttf_start");
extern const uint8_t inter_semibold_end[]   asm("_binary_Inter_SemiBold_ttf_end");
extern const uint8_t inter_bold_start[]     asm("_binary_Inter_Bold_ttf_start");
extern const uint8_t inter_bold_end[]       asm("_binary_Inter_Bold_ttf_end");

typedef enum { W_REGULAR, W_SEMIBOLD, W_BOLD, W_COUNT } weight_t;

static const struct {
    const uint8_t *start;
    const uint8_t *end;
} s_ttf[W_COUNT] = {
    [W_REGULAR]  = { inter_regular_start,  inter_regular_end },
    [W_SEMIBOLD] = { inter_semibold_start, inter_semibold_end },
    [W_BOLD]     = { inter_bold_start,     inter_bold_end },
};

/* The head unit uses seven weight/size pairs; the rest is headroom. */
#define FONT_SLOTS       16
#define FONT_GLYPH_CACHE 48

/* KnobFace.qml */
#define LABEL_PX     14
#define LABEL_SPACE  1      /* letterSpacing 0.6, rounded */
#define VALUE_SPACE  (-2)
#define UNIT_PX      22
#define UNIT_GAP     2
#define SUB_PX       16
#define COL_GAP      2
#define GROUP_GAP    4      /* between the centred value or icon and the rows above and below */
/* Text stays inside this radius: the scales take the ring outside it */
#define TEXT_R       96.0f
#define PAGER_TOP    8
#define PAGER_GAP    6
#define DOT          8
#define DOT_ON_W     26
#define MAX_DOTS     12

/* The scale round the edge. Angles are degrees clockwise from 12 o'clock. */
#define SCALE_SWEEP  135.0f  /* each side of 12 o'clock; open at the bottom */
#define SCALE_R      111.0f  /* centre line of the arcs */
#define SCALE_W      6       /* arc width */
#define SPLIT_CUT    2.2f    /* the neutral cut leans this far, and is as wide */
#define BEAD_R       7.5f
#define BEAD_RING    3.0f    /* screen-coloured ring parting the bead from the arc */
#define DOTS_SWEEP   90.0f
#define DOTS_R       108.0f
#define DOT_MIN_R    3.0f
#define DOT_MAX_R    9.0f
#define SELECT_GAP   9.0f
#define RAMP_W_MIN   2.0f    /* the volume wedge's width at its left end */
#define RAMP_W_MAX   14.0f   /* and at its right end */
#define RAMP_STEP    3.0f    /* degrees per slice */

/* KnobFace.qml's measurements are for the 240 px display the first dial
 * had. A bigger display draws the same face scaled up to DIAL_BOARD_FACE_PX,
 * centred, so the two dials look alike. The measurements above are design
 * pixels; PX() and PXF() turn them into screen pixels. */
#define FACE_K   ((float)DIAL_BOARD_FACE_PX / 240.0f)
#define PX(v)    ((int32_t)lroundf((v) * FACE_K))
#define PXF(v)   ((v) * FACE_K)

static struct {
    weight_t   w;
    int        px;
    lv_font_t *font;
} s_fonts[FONT_SLOTS];
static int s_font_count;

static const lv_font_t *font(weight_t w, int px)
{
    if (px < 8) {
        px = 8;
    } else if (px > PX(120)) {
        px = PX(120);
    }
    for (int i = 0; i < s_font_count; i++) {
        if (s_fonts[i].w == w && s_fonts[i].px == px) {
            return s_fonts[i].font;
        }
    }
    if (s_font_count == FONT_SLOTS) {
        ESP_LOGW(TAG, "font slots full; %d px drawn at %d px", px, s_fonts[0].px);
        return s_fonts[0].font;
    }
    lv_font_t *f = lv_tiny_ttf_create_data_ex(s_ttf[w].start,
                                              s_ttf[w].end - s_ttf[w].start,
                                              px, LV_FONT_KERNING_NORMAL,
                                              FONT_GLYPH_CACHE);
    if (!f) {
        ESP_LOGE(TAG, "TinyTTF failed at %d px", px);
        return LV_FONT_DEFAULT;
    }
    s_fonts[s_font_count++] = (typeof(s_fonts[0])){ w, px, f };
    return f;
}

/* Theme.qml, light and dark. */
typedef struct {
    lv_color_t screen, tx, tx2, acc, heat, cool, warn, line;
} theme_t;

static theme_t theme(bool dark)
{
    if (dark) {
        return (theme_t){
            lv_color_hex(0x000000), lv_color_hex(0xFFFFFF), lv_color_hex(0xAAAAAA),
            lv_color_hex(0x7BC96A), lv_color_hex(0xFF5453), lv_color_hex(0x48E6FE),
            lv_color_hex(0xFFC107), lv_color_hex(0x3A3A3A),
        };
    }
    return (theme_t){
        lv_color_hex(0xFFFFFF), lv_color_hex(0x1A1A1A), lv_color_hex(0x4A4A4A),
        lv_color_hex(0x3D7B31), lv_color_hex(0xC62828), lv_color_hex(0x00708A),
        lv_color_hex(0x8A5A00), lv_color_hex(0xCACACA),
    };
}

static lv_color_t named(const theme_t *t, uint8_t c)
{
    switch (c) {
    case DIAL_COLOR_ACC:  return t->acc;
    case DIAL_COLOR_HEAT: return t->heat;
    case DIAL_COLOR_COOL: return t->cool;
    case DIAL_COLOR_WARN: return t->warn;
    default:              return t->tx;
    }
}

/* The value, or without one the icon, is centred on the display; the top
 * group (label, and the icon when there is a value) sits above it and the
 * bottom group (sub, pager) below. */
static lv_obj_t *s_top, *s_bottom, *s_label, *s_icon, *s_icon_top, *s_value, *s_unit, *s_sub, *s_pager;
static lv_obj_t *s_dots[MAX_DOTS];

/* What the scale shows; drawn under the text by draw_scale(). */
static struct {
    uint8_t    kind, steps, pos, color;
    bool       dark;
    theme_t    t;
    lv_color_t fill;     /* the ramp's filled part */
} s_scale;

/* The point deg clockwise from 12 o'clock, r design pixels from the centre. */
static lv_point_precise_t at(float deg, float r)
{
    const float a = deg * (float)M_PI / 180.0f;
    return (lv_point_precise_t){
        .x = (lv_value_precise_t)lroundf(DIAL_BOARD_LCD_H_RES / 2 + PXF(r) * sinf(a)),
        .y = (lv_value_precise_t)lroundf(DIAL_BOARD_LCD_V_RES / 2 - PXF(r) * cosf(a)),
    };
}

/* An arc of the scale from a to b, clockwise. */
static void arc(lv_layer_t *layer, float a, float b, lv_color_t c, bool rounded)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = DIAL_BOARD_LCD_H_RES / 2;
    d.center.y = DIAL_BOARD_LCD_V_RES / 2;
    d.radius = (uint16_t)lroundf(PXF(SCALE_R + SCALE_W / 2.0f));   /* LVGL's radius is the outer edge */
    d.width = PX(SCALE_W);
    /* LVGL counts from 3 o'clock */
    d.start_angle = (lv_value_precise_t)lroundf(fmodf(a - 90.0f + 360.0f, 360.0f));
    d.end_angle = (lv_value_precise_t)lroundf(fmodf(b - 90.0f + 360.0f, 360.0f));
    d.color = c;
    d.rounded = rounded;
    lv_draw_arc(layer, &d);
}

/* A filled circle of r design pixels at p. */
static void disc(lv_layer_t *layer, lv_point_precise_t p, float r, lv_color_t c)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.radius = LV_RADIUS_CIRCLE;
    const int32_t ri = (int32_t)lroundf(PXF(r));
    const lv_area_t box = { p.x - ri, p.y - ri, p.x + ri, p.y + ri };
    lv_draw_rect(layer, &d, &box);
}

/* Lengthen the segment a-b by half again at each end, along its own line,
 * so a cut drawn on it clears the arc's antialiased edges. */
static void stretch(lv_point_precise_t *a, lv_point_precise_t *b)
{
    const lv_value_precise_t dx = (b->x - a->x) / 2, dy = (b->y - a->y) / 2;
    a->x -= dx;
    a->y -= dy;
    b->x += dx;
    b->y += dy;
}

static void triangle(lv_layer_t *layer, lv_point_precise_t a, lv_point_precise_t b,
                     lv_point_precise_t c, lv_color_t color)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.p[0] = a;
    d.p[1] = b;
    d.p[2] = c;
    d.bg_color = color;
    lv_draw_triangle(layer, &d);
}

/* Cool on the left, heat on the right, meeting at 12 o'clock on a slanted
 * cut, with a bead at the setting. The arcs end square at 12 o'clock and the
 * cut is a parallelogram in the screen colour: its left edge runs from 12
 * o'clock on the outside to SPLIT_CUT degrees left on the inside, its right
 * edge from SPLIT_CUT right on the outside to 12 o'clock on the inside, so
 * neither colour shows on the wrong side of it. */
static void draw_split(lv_layer_t *layer, const theme_t *t)
{
    arc(layer, -SCALE_SWEEP, 0, t->cool, false);
    arc(layer, 0, SCALE_SWEEP, t->heat, false);
    disc(layer, at(-SCALE_SWEEP, SCALE_R), SCALE_W / 2.0f, t->cool);
    disc(layer, at(SCALE_SWEEP, SCALE_R), SCALE_W / 2.0f, t->heat);

    const float ro = SCALE_R + SCALE_W / 2.0f, ri = SCALE_R - SCALE_W / 2.0f;
    lv_point_precise_t l_out = at(0, ro), l_in = at(-SPLIT_CUT, ri);
    lv_point_precise_t r_out = at(SPLIT_CUT, ro), r_in = at(0, ri);
    stretch(&l_out, &l_in);
    stretch(&r_out, &r_in);
    triangle(layer, l_out, r_out, r_in, t->screen);
    triangle(layer, l_out, r_in, l_in, t->screen);

    const int n = s_scale.steps;
    if (n < 2 || s_scale.pos >= n) {
        return;
    }
    const int mid = (n - 1) / 2;
    const float deg = -SCALE_SWEEP + 2 * SCALE_SWEEP * s_scale.pos / (n - 1);
    const lv_point_precise_t p = at(deg, SCALE_R);
    disc(layer, p, BEAD_R + BEAD_RING, t->screen);
    disc(layer, p, BEAD_R, s_scale.pos < mid ? t->cool : s_scale.pos > mid ? t->heat : t->tx);
}

/* Dots across the top growing left to right, lit up to the setting. */
static void draw_dots(lv_layer_t *layer, const theme_t *t)
{
    const int n = s_scale.steps;
    for (int i = 0; i < n; i++) {
        const float f = n > 1 ? (float)i / (n - 1) : 1;
        disc(layer, at(-DOTS_SWEEP + 2 * DOTS_SWEEP * f, DOTS_R),
             DOT_MIN_R + (DOT_MAX_R - DOT_MIN_R) * f, i < s_scale.pos ? t->acc : t->line);
    }
}

/* Equal segments, the chosen one lit. */
static void draw_select(lv_layer_t *layer, const theme_t *t)
{
    const int n = s_scale.steps;
    if (n < 1) {
        return;
    }
    const float seg = (2 * SCALE_SWEEP - (n - 1) * SELECT_GAP) / n;
    for (int i = 0; i < n; i++) {
        const float a = -SCALE_SWEEP + i * (seg + SELECT_GAP);
        arc(layer, a, a + seg, i == s_scale.pos ? t->acc : t->line, true);
    }
}

/* The volume wedge: the outer edge on the scale's outer circle, the band
 * widening clockwise from RAMP_W_MIN to RAMP_W_MAX, in slices; filled up to
 * the setting, the rest in the track colour. */
static void draw_ramp(lv_layer_t *layer, const theme_t *t)
{
    const int n = s_scale.steps;
    const float ro = SCALE_R + SCALE_W / 2.0f;
    const float fill = -SCALE_SWEEP + 2 * SCALE_SWEEP * (n > 0 ? (float)s_scale.pos / n : 0);
    const lv_color_t on = s_scale.fill;
    for (float a = -SCALE_SWEEP; a < SCALE_SWEEP - 0.01f; a += RAMP_STEP) {
        const float b = fminf(a + RAMP_STEP, SCALE_SWEEP);
        /* A slice straddling the setting is drawn in two */
        const float cuts[3] = { a, (fill > a && fill < b) ? fill : b, b };
        for (int k = 0; k < 2; k++) {
            const float x = cuts[k], y = cuts[k + 1];
            if (y - x < 0.01f) {
                continue;
            }
            const float wx = RAMP_W_MIN + (RAMP_W_MAX - RAMP_W_MIN) * (x + SCALE_SWEEP) / (2 * SCALE_SWEEP);
            const float wy = RAMP_W_MIN + (RAMP_W_MAX - RAMP_W_MIN) * (y + SCALE_SWEEP) / (2 * SCALE_SWEEP);
            /* Overlap the next slice slightly so no seam shows */
            const float y2 = fminf(y + 0.4f, SCALE_SWEEP);
            const lv_color_t c = (x + y) / 2 < fill ? on : t->line;
            const lv_point_precise_t o1 = at(x, ro), o2 = at(y2, ro);
            const lv_point_precise_t i1 = at(x, ro - wx), i2 = at(y2, ro - wy);
            triangle(layer, o1, o2, i2, c);
            triangle(layer, o1, i2, i1, c);
        }
    }
    /* A rounded tip at the thin end */
    disc(layer, at(-SCALE_SWEEP, ro - RAMP_W_MIN / 2), RAMP_W_MIN / 2, s_scale.pos > 0 ? on : t->line);
}

static void draw_scale(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    switch (s_scale.kind) {
    case DIAL_SCALE_SPLIT:  draw_split(layer, &s_scale.t);  break;
    case DIAL_SCALE_DOTS:   draw_dots(layer, &s_scale.t);   break;
    case DIAL_SCALE_SELECT: draw_select(layer, &s_scale.t); break;
    case DIAL_SCALE_RAMP:   draw_ramp(layer, &s_scale.t);   break;
    default:                break;
    }
}

/* A bare container sized to its children. */
static lv_obj_t *box(lv_obj_t *parent, lv_flex_flow_t flow)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(o, flow);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

/* Set text in a font at its natural width; fit() narrows it later. */
static void set_text(lv_obj_t *l, const char *txt, const lv_font_t *f,
                     int32_t space, lv_color_t color)
{
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_letter_space(l, space, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(l, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_label_set_text(l, txt);
}

/* The width of the circle of radius TEXT_R across the band y1..y2, at its
 * edge farther from the centre. */
static int32_t chord(int32_t y1, int32_t y2)
{
    const float c = DIAL_BOARD_LCD_V_RES / 2.0f, tr = PXF(TEXT_R);
    const float dy = fmaxf(fabsf(y1 - c), fabsf(y2 + 1 - c));
    return dy >= tr ? 0 : (int32_t)(2 * sqrtf(tr * tr - dy * dy));
}

/* Elide a label that is wider than the circle where it sits --
 * Text { width: Math.min(implicitWidth, chord); elide: Text.ElideRight }. */
static void fit(lv_obj_t *l, int32_t reserve)
{
    if (lv_obj_has_flag(l, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    lv_area_t a;
    lv_obj_get_coords(l, &a);
    const int32_t max_w = chord(a.y1, a.y2) - reserve;
    if (lv_area_get_width(&a) > max_w) {
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_size(l, max_w > 0 ? max_w : 0, lv_area_get_height(&a));
    }
}

static void show_if(lv_obj_t *o, bool visible)
{
    if (visible) {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

void face_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    /* After the background, before the text */
    lv_obj_add_event_cb(scr, draw_scale, LV_EVENT_DRAW_MAIN_END, NULL);

    /* The centred value (and the unit hanging off its right) or icon */
    s_value = label(scr);
    s_unit  = label(scr);
    /* STRETCH: an icon the head unit sent smaller than the face shows it
     * (face_icon_host_px) is scaled up to the object's size. */
    s_icon = lv_image_create(scr);
    lv_obj_set_style_image_recolor_opa(s_icon, LV_OPA_COVER, 0);
    lv_image_set_inner_align(s_icon, LV_IMAGE_ALIGN_STRETCH);

    s_top = box(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_top, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_top, PX(COL_GAP), 0);
    s_label = label(s_top);
    s_icon_top = lv_image_create(s_top);
    lv_obj_set_style_image_recolor_opa(s_icon_top, LV_OPA_COVER, 0);
    lv_image_set_inner_align(s_icon_top, LV_IMAGE_ALIGN_STRETCH);

    s_bottom = box(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_bottom, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_bottom, PX(COL_GAP), 0);
    s_sub = label(s_bottom);

    s_pager = box(s_bottom, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_top(s_pager, PX(PAGER_TOP), 0);
    lv_obj_set_style_pad_column(s_pager, PX(PAGER_GAP), 0);
    for (int i = 0; i < MAX_DOTS; i++) {
        lv_obj_t *d = lv_obj_create(s_pager);
        lv_obj_remove_style_all(d);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(d, PX(DOT) / 2, 0);
        lv_obj_set_size(d, PX(DOT), PX(DOT));
        s_dots[i] = d;
    }

    /* Draw the TinyTTF fonts the face always uses once now, so the first
     * face from the head unit does not wait on them. */
    font(W_BOLD, PX(LABEL_PX));
    font(W_SEMIBOLD, PX(SUB_PX));
    font(W_REGULAR, PX(UNIT_PX));
}

/* The value, or else the icon, at the centre; the unit off the value's right
 * on its baseline; the groups above and below; then every line narrowed to
 * the circle where it ended up. */
static void place(bool has_value, bool has_icon, const lv_font_t *vf, const lv_font_t *uf)
{
    lv_obj_t *mid = has_value ? s_value : has_icon ? s_icon : NULL;
    lv_obj_update_layout(lv_screen_active());
    for (int pass = 0; pass < 2; pass++) {
        if (mid) {
            lv_obj_align(mid, LV_ALIGN_CENTER, 0, 0);
            lv_obj_align_to(s_top, mid, LV_ALIGN_OUT_TOP_MID, 0, -PX(GROUP_GAP));
            lv_obj_align_to(s_bottom, mid, LV_ALIGN_OUT_BOTTOM_MID, 0, PX(GROUP_GAP));
        } else {
            /* Only text: the two groups meet at the centre */
            lv_obj_align(s_top, LV_ALIGN_BOTTOM_MID, 0, -(DIAL_BOARD_LCD_V_RES / 2 + PX(GROUP_GAP) / 2));
            lv_obj_align(s_bottom, LV_ALIGN_TOP_MID, 0, DIAL_BOARD_LCD_V_RES / 2 + PX(GROUP_GAP) / 2);
        }
        if (has_value) {
            lv_obj_align_to(s_unit, s_value, LV_ALIGN_OUT_RIGHT_BOTTOM, PX(UNIT_GAP), 0);
            lv_obj_set_style_translate_y(s_unit, -(vf->base_line - uf->base_line), 0);
        }
        lv_obj_update_layout(lv_screen_active());
        if (pass == 0) {
            fit(s_label, 0);
            fit(s_sub, 0);
            if (has_value) {
                lv_area_t u;
                lv_obj_get_coords(s_unit, &u);
                const bool unit = !lv_obj_has_flag(s_unit, LV_OBJ_FLAG_HIDDEN);
                /* The unit hangs off one side, so the value keeps both sides clear of it */
                fit(s_value, unit ? 2 * (lv_area_get_width(&u) + PX(UNIT_GAP)) : 0);
            }
        }
    }
}

void face_show(const face_t *f, const lv_image_dsc_t *icon)
{
    const dial_face_t *st = &f->style;
    const theme_t t = theme(st->flags & DIAL_FACE_DARK);
    const lv_color_t vc = named(&t, st->color);

    lv_obj_set_style_bg_color(lv_screen_active(), t.screen, 0);

    const bool dark = st->flags & DIAL_FACE_DARK;
    if (st->scale != s_scale.kind || st->scale_steps != s_scale.steps ||
        st->scale_pos != s_scale.pos || dark != s_scale.dark || st->color != s_scale.color) {
        s_scale.kind  = st->scale;
        s_scale.steps = st->scale_steps;
        s_scale.pos   = st->scale_pos;
        s_scale.dark  = dark;
        s_scale.t     = t;
        s_scale.color = st->color;
        s_scale.fill  = st->color == DIAL_COLOR_TX ? t.acc : vc;
        lv_obj_invalidate(lv_screen_active());
    }

    const char *value = f->text[DIAL_FIELD_VALUE];
    const bool has_value = value[0] != '\0';
    const bool has_icon = f->text[DIAL_FIELD_ICON][0] != '\0';

    show_if(s_label, f->text[DIAL_FIELD_LABEL][0] != '\0');
    set_text(s_label, f->text[DIAL_FIELD_LABEL], font(W_BOLD, PX(LABEL_PX)), PX(LABEL_SPACE), t.tx2);

    /* The icon is the centrepiece without a value, and sits above one */
    lv_obj_t *icon_on = has_value ? s_icon_top : s_icon;
    lv_obj_t *icon_off = has_value ? s_icon : s_icon_top;
    show_if(icon_off, false);
    show_if(icon_on, has_icon);
    if (has_icon) {
        lv_image_set_src(icon_on, icon);
        lv_obj_set_size(icon_on, PX(st->icon_px), PX(st->icon_px));
        lv_obj_set_style_image_recolor(icon_on, vc, 0);
    }

    show_if(s_value, has_value);
    show_if(s_unit, has_value && f->text[DIAL_FIELD_UNIT][0] != '\0');
    const lv_font_t *vf = font((st->flags & DIAL_FACE_SMALL) ? W_BOLD : W_REGULAR, PX(st->value_px));
    /* A degree sign at the value's own size sits up where it belongs */
    const bool degree = strcmp(f->text[DIAL_FIELD_UNIT], "\xC2\xB0") == 0;
    const lv_font_t *uf = font(W_REGULAR, PX(degree ? st->value_px : UNIT_PX));
    if (has_value) {
        set_text(s_value, value, vf, PX(VALUE_SPACE), vc);
        set_text(s_unit, f->text[DIAL_FIELD_UNIT], uf, 0, t.tx2);
    }

    show_if(s_sub, f->text[DIAL_FIELD_SUB][0] != '\0');
    set_text(s_sub, f->text[DIAL_FIELD_SUB], font(W_SEMIBOLD, PX(SUB_PX)), 0, t.tx2);

    const int dots = st->pager_count < MAX_DOTS ? st->pager_count : MAX_DOTS;
    show_if(s_pager, dots > 0);
    for (int i = 0; i < MAX_DOTS; i++) {
        show_if(s_dots[i], i < dots);
        const bool on = i == st->pager_on;
        lv_obj_set_width(s_dots[i], on ? PX(DOT_ON_W) : PX(DOT));
        lv_obj_set_style_bg_color(s_dots[i], on ? t.acc : t.line, 0);
    }

    place(has_value, has_icon, vf, uf);
}

uint8_t face_icon_host_px(uint8_t icon_px)
{
    const int32_t px = PX(icon_px);
    return px > DIAL_ICON_MAX_PX ? DIAL_ICON_MAX_PX : (uint8_t)px;
}
