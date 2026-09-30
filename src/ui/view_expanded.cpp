#include "ui/view_expanded.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/shell.h"
#include "model/gcode_stats.h"
#include "model/path_loader.h"
#include "model/console_log.h"
#include "model/FluidNCModel.h"
#include "e4math.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// Full width, everything above the safety bar. Hold/resume/reset are the
// controls you hit in a hurry and possibly without looking, so they keep their
// full height and the view takes whatever is left rather than a percentage.
static const lv_coord_t VIEW_W    = SCREEN_W;
static const lv_coord_t VIEW_H    = SCREEN_H - SAFETY_H;
static const lv_coord_t LOG_ROW_H = 24;
static const int        LOG_LINES = (VIEW_H - U * 4) / LOG_ROW_H;

static lv_obj_t* s_root;
static lv_obj_t* s_st_page;
static lv_obj_t* s_con_page;
static lv_obj_t* s_st_title;
static lv_obj_t* s_st_dro;
static lv_obj_t* s_st_keys;
static lv_obj_t* s_st_job;
static lv_obj_t* s_st_setup;
static lv_obj_t* s_log_lbl[LOG_LINES];
static lv_obj_t* s_filter_lbl;

static bool s_open = false;
static expanded_face_t s_face = EXPANDED_STATS;

bool expanded_is_open() { return s_open; }

// "1 h 12 min", "14 min", "40 s": minutes are the unit that matters at the
// machine, so seconds only appear when there is nothing bigger to say.
static void fmt_time(char* b, size_t n, float sec) {
    int m = (int)(sec / 60.0f + 0.5f);
    if (sec < 60.0f)  snprintf(b, n, "%d s", (int)(sec + 0.5f));
    else if (m < 60)  snprintf(b, n, "%d min", m);
    else              snprintf(b, n, "%d h %d min", m / 60, m % 60);
}

static const char* file_name() {
    const char* p = pathload_path();
    const char* slash = strrchr(p, '/');
    return slash ? slash + 1 : p;
}

void stats_summary(char* out, size_t n) {
    const gstats_t& s = gstats();
    if (!s.lines) { snprintf(out, n, "no file scanned"); return; }
    char t[24];
    fmt_time(t, sizeof(t), s.cut_sec);
    if (s.have_cut)
        snprintf(out, n, "%.1f x %.1f mm\n%.2f mm deep\n%u lines, %d tool%s\n~%s cutting\ntap for details",
                 (double)(s.max_x - s.min_x), (double)(s.max_y - s.min_y), (double)-s.min_z,
                 (unsigned)s.lines, s.n_tools, s.n_tools == 1 ? "" : "s", t);
    else
        snprintf(out, n, "%u lines\nno cutting moves\ntap for details", (unsigned)s.lines);
}

void stats_refresh() {
    if (!s_st_job) return;
    const gstats_t& s = gstats();
    lv_label_set_text(s_st_title, s.lines ? file_name() : "no file scanned");
    if (!s.lines) {
        lv_label_set_text(s_st_keys, "");
        lv_label_set_text(s_st_job, "");
        lv_label_set_text(s_st_setup, "");
        return;
    }

    // Two labels, keys and values: the font is proportional, so padding with
    // spaces never lines a column up.
    static char keys[256], job[640];
    int kk = 0, k = 0;
    auto row = [&](const char* key, const char* fmt, ...) {
        kk += snprintf(keys + kk, sizeof(keys) - kk, "%s\n", key);
        va_list va;
        va_start(va, fmt);
        k += vsnprintf(job + k, sizeof(job) - k, fmt, va);
        va_end(va);
        k += snprintf(job + k, sizeof(job) - k, "\n");
    };
    char t[24];
    fmt_time(t, sizeof(t), s.cut_sec);
    row("Lines", "%u   (%u moves)", (unsigned)s.lines, (unsigned)s.moves);
    if (s.have_cut) {
        row("Size X", "%.2f   (%.2f to %.2f)", (double)(s.max_x - s.min_x), (double)s.min_x, (double)s.max_x);
        row("Size Y", "%.2f   (%.2f to %.2f)", (double)(s.max_y - s.min_y), (double)s.min_y, (double)s.max_y);
        row("Deepest Z", "%.2f", (double)s.min_z);
    } else {
        row("Cutting", "none in this file");
    }
    if (s.moves) row("Clearance Z", "%.2f", (double)s.top_z);
    row("Cut length", "%.0f mm", (double)s.cut_mm);
    row("Cut time", "~%s  (rapids not counted)", t);
    if (s.feed_max > 0.0f) row("Feed", "%.0f - %.0f mm/min", (double)s.feed_min, (double)s.feed_max);
    if (s.spindle_max > 0.0f) row("Spindle max", "%.0f", (double)s.spindle_max);
    row("Tool changes", "%d", s.tool_changes);
    if (s.inches) row("Units", "file is inches, shown in mm");
    lv_label_set_text(s_st_keys, keys);
    lv_label_set_text(s_st_job, job);

    static char setup[GSTATS_NOTES * GSTATS_NOTE_LEN + GSTATS_TOOLS * 48 + 64];
    k = 0;
    if (s.n_tools) {
        k += snprintf(setup + k, sizeof(setup) - k, "TOOLS\n");
        for (int i = 0; i < s.n_tools; i++)
            k += snprintf(setup + k, sizeof(setup) - k, "T%d  %s\n", s.tools[i].num, s.tools[i].desc);
        k += snprintf(setup + k, sizeof(setup) - k, "\n");
    }
    k += snprintf(setup + k, sizeof(setup) - k, "FROM THE FILE HEADER\n");
    if (!s.n_notes) k += snprintf(setup + k, sizeof(setup) - k, "(none)\n");
    for (int i = 0; i < s.n_notes; i++)
        k += snprintf(setup + k, sizeof(setup) - k, "%s\n", s.notes[i]);
    lv_label_set_text(s_st_setup, setup);
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
    lv_obj_set_size(s_root, VIEW_W, VIEW_H);
    lv_obj_set_style_bg_color(s_root, C_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    // --- stats face ---
    s_st_page = lv_obj_create(s_root);
    w_flatten(s_st_page);
    lv_obj_set_size(s_st_page, VIEW_W, VIEW_H);
    lv_obj_set_style_pad_all(s_st_page, U + 2, 0);

    s_st_title = w_label(s_st_page, "no file scanned", &lv_font_montserrat_16, C_ACCENT);
    lv_obj_align(s_st_title, LV_ALIGN_TOP_LEFT, 0, 0);

    // The DRO does not vanish when the view expands, it moves. Losing position
    // readout on the screen you watch during a cut is a bad trade.
    s_st_dro = w_label(s_st_page, "X 0.000  Y 0.000  Z 0.000", &lv_font_montserrat_14, C_TEXT);
    lv_obj_align(s_st_dro, LV_ALIGN_TOP_RIGHT, 0, 0);

    const lv_coord_t top = U * 4, col_w = (VIEW_W - U * 5) / 2, col_h = VIEW_H - top - U * 3;
    const lv_coord_t key_w = U * 14;
    s_st_keys = w_label(s_st_page, "", &lv_font_montserrat_14, C_DIM);
    lv_obj_set_pos(s_st_keys, 0, top);
    s_st_job = w_label(s_st_page, "", &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_pos(s_st_job, key_w, top);
    lv_obj_set_width(s_st_job, col_w - key_w);

    // The header can run long (VCarve lists every toolpath), so this column
    // scrolls rather than clipping what the CAM program wrote.
    lv_obj_t* right = lv_obj_create(s_st_page);
    w_flatten(right);
    lv_obj_set_pos(right, col_w + U * 2, top);
    lv_obj_set_size(right, col_w, col_h);
    lv_obj_add_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    s_st_setup = w_label(right, "", &lv_font_montserrat_12, C_DIM);
    lv_obj_set_width(s_st_setup, LV_PCT(100));
    lv_label_set_long_mode(s_st_setup, LV_LABEL_LONG_WRAP);

    // --- console face ---
    s_con_page = lv_obj_create(s_root);
    w_flatten(s_con_page);
    lv_obj_set_size(s_con_page, VIEW_W, VIEW_H);
    lv_obj_add_flag(s_con_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_con_page, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* head = lv_obj_create(s_con_page);
    w_flatten(head);
    lv_obj_set_size(head, VIEW_W, U * 4);
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
    lv_obj_set_size(logbox, VIEW_W, LOG_LINES * LOG_ROW_H);
    lv_obj_set_style_pad_left(logbox, U + 4, 0);
    lv_obj_set_flex_flow(logbox, LV_FLEX_FLOW_COLUMN);
    for (int i = 0; i < LOG_LINES; i++) {
        s_log_lbl[i] = w_label(logbox, "", &lv_font_montserrat_12, C_DIM);
        lv_obj_set_height(s_log_lbl[i], LOG_ROW_H);
    }
}

void expanded_open(expanded_face_t face) {
    s_open = true;
    s_face = face;

    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);

    if (face == EXPANDED_STATS) {
        lv_obj_clear_flag(s_st_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_con_page, LV_OBJ_FLAG_HIDDEN);
        stats_refresh();
    } else {
        lv_obj_add_flag(s_st_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_con_page, LV_OBJ_FLAG_HIDDEN);
    }

    // Last: this reparents the safety bar to the screen and raises it, so it
    // must run after s_root has already come forward or the stop controls end
    // up behind the view.
    shell_set_expanded(true);
}

void expanded_close() {
    s_open = false;
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    shell_set_expanded(false);
}

void expanded_tick() {
    if (!s_open) return;

    if (s_face == EXPANDED_STATS) {
        char b[80];
        snprintf(b, sizeof(b), "X %s  Y %s  Z %s",
                 e4_to_cstr(myAxes[0], 3), e4_to_cstr(myAxes[1], 3), e4_to_cstr(myAxes[2], 3));
        lv_label_set_text(s_st_dro, b);
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
