/*
 * The round display. See face.h.
 *
 * Text is drawn with LVGL's TinyTTF from the head unit's own Inter TTFs
 * (embedded from hmi/assets/fonts), so the dial can show any size the head
 * unit asks for and any character a contact name or network name contains.
 * Each weight and size is rasterised on first use and kept.
 *
 * Measurements mirror hmi/qml/knob/KnobFace.qml. Change them together.
 */

#include <string.h>

#include "esp_log.h"

#include "crowpanel_board.h"
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
#define LABEL_PX     19
#define LABEL_MAX_W  164
#define LABEL_SPACE  1      /* letterSpacing 0.6, rounded */
#define VALUE_MAX_W  196
#define VALUE_SPACE  (-2)
#define UNIT_PX      30
#define SUB_PX       19
#define SUB_MAX_W    170
#define COL_GAP      2
#define PAGER_TOP    8
#define PAGER_GAP    6
#define DOT          8
#define DOT_ON_W     26
#define MAX_DOTS     12

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
    } else if (px > 120) {
        px = 120;
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

static lv_obj_t *s_label, *s_icon, *s_row, *s_value, *s_unit, *s_sub, *s_pager;
static lv_obj_t *s_dots[MAX_DOTS];

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

/* Set text in a font, at its natural width up to max_w, then elided --
 * Text { width: Math.min(implicitWidth, max); elide: Text.ElideRight }. */
static void set_text(lv_obj_t *l, const char *txt, const lv_font_t *f,
                     int32_t space, int32_t max_w, lv_color_t color)
{
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_letter_space(l, space, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt);

    lv_point_t sz;
    lv_text_get_size(&sz, txt, f, space, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x > max_w) {
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_size(l, max_w, lv_font_get_line_height(f));
    } else {
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_obj_set_size(l, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
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

    lv_obj_t *col = box(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_center(col);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, COL_GAP, 0);

    s_label = label(col);

    s_icon = lv_image_create(col);
    lv_obj_set_style_image_recolor_opa(s_icon, LV_OPA_COVER, 0);

    /* Value and unit share a baseline: bottoms aligned here, then the unit
     * lifted by the difference in baselines in face_show(). */
    s_row = box(col, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    s_value = label(s_row);
    s_unit  = label(s_row);

    s_sub = label(col);

    s_pager = box(col, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_top(s_pager, PAGER_TOP, 0);
    lv_obj_set_style_pad_column(s_pager, PAGER_GAP, 0);
    for (int i = 0; i < MAX_DOTS; i++) {
        lv_obj_t *d = lv_obj_create(s_pager);
        lv_obj_remove_style_all(d);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(d, DOT / 2, 0);
        lv_obj_set_size(d, DOT, DOT);
        s_dots[i] = d;
    }

    /* Draw the TinyTTF fonts the face always uses once now, so the first
     * face from the head unit does not wait on them. */
    font(W_BOLD, LABEL_PX);
    font(W_SEMIBOLD, SUB_PX);
    font(W_REGULAR, UNIT_PX);
}

void face_show(const face_t *f, const lv_image_dsc_t *icon)
{
    const dial_face_t *st = &f->style;
    const theme_t t = theme(st->flags & DIAL_FACE_DARK);
    const lv_color_t vc = named(&t, st->color);

    lv_obj_set_style_bg_color(lv_screen_active(), t.screen, 0);

    set_text(s_label, f->text[DIAL_FIELD_LABEL], font(W_BOLD, LABEL_PX),
             LABEL_SPACE, LABEL_MAX_W, t.tx2);

    const bool has_icon = f->text[DIAL_FIELD_ICON][0] != '\0';
    show_if(s_icon, has_icon);
    if (has_icon) {
        lv_image_set_src(s_icon, icon);
        lv_obj_set_size(s_icon, st->icon_px, st->icon_px);
        lv_obj_set_style_image_recolor(s_icon, vc, 0);
    }

    const char *value = f->text[DIAL_FIELD_VALUE];
    show_if(s_row, value[0] != '\0');
    if (value[0]) {
        const lv_font_t *vf = font((st->flags & DIAL_FACE_SMALL) ? W_BOLD : W_REGULAR,
                                   st->value_px);
        const lv_font_t *uf = font(W_REGULAR, UNIT_PX);
        set_text(s_value, value, vf, VALUE_SPACE, VALUE_MAX_W, vc);
        const char *unit = f->text[DIAL_FIELD_UNIT];
        show_if(s_unit, unit[0] != '\0');
        set_text(s_unit, unit, uf, 0, CROWPANEL_LCD_H_RES, t.tx2);
        lv_obj_set_style_translate_y(s_unit, -(vf->base_line - uf->base_line), 0);
    }

    set_text(s_sub, f->text[DIAL_FIELD_SUB], font(W_SEMIBOLD, SUB_PX), 0,
             SUB_MAX_W, t.tx2);

    const int dots = st->pager_count < MAX_DOTS ? st->pager_count : MAX_DOTS;
    show_if(s_pager, dots > 0);
    for (int i = 0; i < MAX_DOTS; i++) {
        show_if(s_dots[i], i < dots);
        const bool on = i == st->pager_on;
        lv_obj_set_width(s_dots[i], on ? DOT_ON_W : DOT);
        lv_obj_set_style_bg_color(s_dots[i], on ? t.acc : t.line, 0);
    }
}
