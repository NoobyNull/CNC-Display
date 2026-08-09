#include "ui/view_expanded.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/shell.h"
#include "model/gcode_path.h"
#include "model/console_log.h"
#include "model/FluidNCModel.h"
#include "e4math.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// The canvas is 800x400: full width, everything above the safety bar. 90% of the
// panel height would be 432px, which would leave 48px for hold/resume/reset —
// the controls you hit in a hurry and possibly without looking. They keep their
// full 80px and the canvas takes the rest.
#define CANVAS_W  800
#define CANVAS_H  400
#define LOG_LINES 13

static lv_obj_t* s_root;
static lv_obj_t* s_tp_page;
static lv_obj_t* s_con_page;
static lv_obj_t* s_canvas;
static lv_obj_t* s_marker;
static lv_obj_t* s_tp_info;
static lv_obj_t* s_tp_dro;
static lv_obj_t* s_log_lbl[LOG_LINES];
static lv_obj_t* s_filter_lbl;

static bool s_open = false;
static expanded_face_t s_face = EXPANDED_TOOLPATH;
static lv_color_t* s_cbuf = nullptr;

static float s_sx = 1.0f, s_sy = 1.0f, s_ox = 0.0f, s_oy = 0.0f;

bool expanded_is_open() { return s_open; }

static void fit_transform() {
    gextents_t e = gpath_extents();
    float w = e.max_x - e.min_x, h = e.max_y - e.min_y;
    if (w <= 0.0f) w = 1.0f;
    if (h <= 0.0f) h = 1.0f;
    const float M = 24.0f;
    float sx = (CANVAS_W - 2 * M) / w;
    float sy = (CANVAS_H - 2 * M) / h;
    float s  = sx < sy ? sx : sy;
    s_sx = s;
    s_sy = s;
    s_ox = M + ((CANVAS_W - 2 * M) - w * s) * 0.5f - e.min_x * s;
    s_oy = M + ((CANVAS_H - 2 * M) - h * s) * 0.5f + e.max_y * s;
}

static inline lv_coord_t px(float x) { return (lv_coord_t)(s_ox + x * s_sx); }
static inline lv_coord_t py(float y) { return (lv_coord_t)(s_oy - y * s_sy); }

void toolpath_redraw() {
    if (!s_canvas || !s_cbuf) return;
    lv_canvas_fill_bg(s_canvas, C_BG, LV_OPA_COVER);
    size_t n = gpath_count();
    if (n < 2) return;
    fit_transform();

    lv_draw_line_dsc_t feed, rapid;
    lv_draw_line_dsc_init(&feed);
    feed.color = C_ACCENT;
    feed.width = 2;
    feed.opa   = LV_OPA_COVER;
    lv_draw_line_dsc_init(&rapid);
    rapid.color = C_LINE;
    rapid.width = 1;
    rapid.opa   = LV_OPA_COVER;

    lv_point_t seg[2];
    for (size_t i = 1; i < n; i++) {
        gpoint_t a = gpath_point(i - 1), b = gpath_point(i);
        seg[0].x = px(a.x); seg[0].y = py(a.y);
        seg[1].x = px(b.x); seg[1].y = py(b.y);
        lv_canvas_draw_line(s_canvas, seg, 2, b.rapid ? &rapid : &feed);
    }
}

void toolpath_set_marker(float x, float y) {
    if (!s_marker || !s_open || s_face != EXPANDED_TOOLPATH) return;
    lv_obj_set_pos(s_marker, px(x) - 8, py(y) - 8);
}

static void filter_cb(lv_event_t*) {
    clog_set_show_status(!clog_show_status());
    lv_label_set_text(s_filter_lbl,
        clog_show_status() ? "STATUS FRAMES SHOWN" : "STATUS FRAMES HIDDEN");
}

static lv_color_t log_colour(clog_kind_t k) {
    switch (k) {
        case CLOG_SENT:  return C_TEXT;
        case CLOG_OK:    return lv_color_hex(0x48695A);
        case CLOG_ERROR: return C_CRIT;
        case CLOG_ALARM: return C_CRIT;
        case CLOG_MSG:   return C_WARN;
        default:         return C_DIM;
    }
}

void expanded_build() {
    s_root = lv_obj_create(lv_scr_act());
    w_flatten(s_root);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_size(s_root, CANVAS_W, CANVAS_H);
    lv_obj_set_style_bg_color(s_root, C_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    // --- toolpath face ---
    s_tp_page = lv_obj_create(s_root);
    w_flatten(s_tp_page);
    lv_obj_set_size(s_tp_page, CANVAS_W, CANVAS_H);

    // 800*400*2 bytes is 640KB — far past internal RAM, so PSRAM it is.
    s_cbuf = (lv_color_t*)ps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_W, CANVAS_H));
    s_canvas = lv_canvas_create(s_tp_page);
    if (s_cbuf) {
        lv_canvas_set_buffer(s_canvas, s_cbuf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(s_canvas, C_BG, LV_OPA_COVER);
    } else {
        Serial.println("[expanded] canvas buffer alloc FAILED");
    }
    lv_obj_set_pos(s_canvas, 0, 0);

    s_marker = lv_obj_create(s_tp_page);
    lv_obj_set_size(s_marker, 16, 16);
    lv_obj_set_style_radius(s_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_marker, C_WARN, 0);
    lv_obj_set_style_bg_opa(s_marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_marker, 0, 0);
    lv_obj_clear_flag(s_marker, LV_OBJ_FLAG_SCROLLABLE);

    s_tp_info = w_label(s_tp_page, "no file loaded", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(s_tp_info, LV_ALIGN_TOP_LEFT, U + 2, U + 2);

    // The DRO does not vanish when the view expands, it moves. Losing position
    // readout on the screen you watch during a cut is a bad trade.
    s_tp_dro = w_label(s_tp_page, "X 0.000  Y 0.000  Z 0.000", &lv_font_montserrat_14, C_TEXT);
    lv_obj_align(s_tp_dro, LV_ALIGN_TOP_RIGHT, -(U + 2), U + 2);

    // --- console face ---
    s_con_page = lv_obj_create(s_root);
    w_flatten(s_con_page);
    lv_obj_set_size(s_con_page, CANVAS_W, CANVAS_H);
    lv_obj_add_flag(s_con_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_con_page, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* head = lv_obj_create(s_con_page);
    w_flatten(head);
    lv_obj_set_size(head, CANVAS_W, U * 4);
    lv_obj_set_style_bg_color(head, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_t* ht = w_label(head, "CONSOLE - UART 1 Mbaud", &lv_font_montserrat_10, C_DIM);
    lv_obj_align(ht, LV_ALIGN_LEFT_MID, U + 2, 0);

    lv_obj_t* fb = lv_btn_create(head);
    lv_obj_set_size(fb, U * 28, U * 3);
    lv_obj_align(fb, LV_ALIGN_RIGHT_MID, -U, 0);
    lv_obj_set_style_bg_opa(fb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(fb, 0, 0);
    lv_obj_add_event_cb(fb, filter_cb, LV_EVENT_CLICKED, nullptr);
    s_filter_lbl = w_label(fb, "STATUS FRAMES HIDDEN", &lv_font_montserrat_10, C_ACCENT);
    lv_obj_center(s_filter_lbl);

    lv_obj_t* logbox = lv_obj_create(s_con_page);
    w_flatten(logbox);
    lv_obj_set_size(logbox, CANVAS_W, LOG_LINES * 24);
    lv_obj_set_style_pad_left(logbox, U + 4, 0);
    lv_obj_set_flex_flow(logbox, LV_FLEX_FLOW_COLUMN);
    for (int i = 0; i < LOG_LINES; i++) {
        s_log_lbl[i] = w_label(logbox, "", &lv_font_montserrat_12, C_DIM);
        lv_obj_set_height(s_log_lbl[i], 24);
    }
}

void expanded_open(expanded_face_t face) {
    s_open = true;
    s_face = face;

    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);

    if (face == EXPANDED_TOOLPATH) {
        lv_obj_clear_flag(s_tp_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_con_page, LV_OBJ_FLAG_HIDDEN);
        toolpath_redraw();
    } else {
        lv_obj_add_flag(s_tp_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_con_page, LV_OBJ_FLAG_HIDDEN);
    }

    // Last: this reparents the safety bar to the screen and raises it, so it
    // must run after s_root has already come forward or the stop controls end
    // up behind the canvas.
    shell_set_expanded(true);
}

void expanded_close() {
    s_open = false;
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    shell_set_expanded(false);
}

void expanded_flip() {
    if (!s_open) return;
    expanded_open(s_face == EXPANDED_TOOLPATH ? EXPANDED_CONSOLE : EXPANDED_TOOLPATH);
}

void expanded_tick() {
    if (!s_open) return;

    if (s_face == EXPANDED_TOOLPATH) {
        char b[80];
        snprintf(b, sizeof(b), "X %s  Y %s  Z %s",
                 e4_to_cstr(myAxes[0], 3), e4_to_cstr(myAxes[1], 3), e4_to_cstr(myAxes[2], 3));
        lv_label_set_text(s_tp_dro, b);

        char info[64];
        snprintf(info, sizeof(info), "%u points", (unsigned)gpath_count());
        lv_label_set_text(s_tp_info, gpath_count() ? info : "no file loaded");
        return;
    }

    if (clog_dirty_take()) {
        size_t n = clog_count();
        size_t first = n > LOG_LINES ? n - LOG_LINES : 0;
        for (int i = 0; i < LOG_LINES; i++) {
            const clog_line_t* l = ((size_t)(first + i) < n) ? clog_line(first + i) : nullptr;
            lv_label_set_text(s_log_lbl[i], l ? l->text : "");
            if (l) lv_obj_set_style_text_color(s_log_lbl[i], log_colour(l->kind), 0);
        }
    }
}
