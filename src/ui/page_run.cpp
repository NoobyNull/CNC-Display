#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/view_expanded.h"
#include "model/filelist.h"
#include "model/FluidNCModel.h"
#include "model/gcode_path.h"
#include "model/path_loader.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <string.h>

#define ROWS 3

static lv_obj_t* s_crumb;
static lv_obj_t* s_row[ROWS];
static lv_obj_t* s_row_name[ROWS];
static lv_obj_t* s_row_size[ROWS];
static lv_obj_t* s_preview_lbl;
static int  s_sel = -1;
static int  s_scroll = 0;
static char s_selected[FL_NAME_MAX] = { 0 };
static bool s_redraw_pending = false;
static bool s_list_dirty = true;

static void row_cb(lv_event_t* e) {
    int r = (int)(intptr_t)lv_event_get_user_data(e);
    const fl_entry_t* en = filelist_at((size_t)(s_scroll + r));
    if (!en) return;
    if (en->is_dir) {
        filelist_enter(en->name);
        s_scroll = 0;
        s_sel = -1;
        s_list_dirty = true;
        return;
    }
    s_sel = s_scroll + r;
    strncpy(s_selected, en->name, sizeof(s_selected) - 1);
    s_selected[sizeof(s_selected) - 1] = 0;

    char full[192];
    snprintf(full, sizeof(full), "%s/%s", filelist_path(), s_selected);
    s_list_dirty = true;                 // highlight the newly selected row
    if (pathload_start(full)) s_redraw_pending = true;
}

static void scroll_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int n = s_scroll + dir;
    int max = (int)filelist_count() - ROWS;
    if (max < 0) max = 0;
    if (n < 0) n = 0;
    if (n > max) n = max;
    if (n != s_scroll) {
        s_scroll = n;
        s_list_dirty = true;   // the model has not changed, but the view has
    }
}

static void run_cb(lv_event_t*) {
    if (!s_selected[0]) return;
    send_linef("$SD/Run=%s/%s", filelist_path(), s_selected);
}

// Check mode runs the file through the parser with no motion, so syntax errors
// and out-of-travel moves surface before the cutter is near the work.
static void dry_cb(lv_event_t*)    { send_line("$C"); }
static void reload_cb(lv_event_t*) { filelist_request(); }
static void expand_cb(lv_event_t*) { expanded_open(EXPANDED_TOOLPATH); }

void page_run_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p, U, 0);

    // --- file list ---
    lv_obj_t* list = lv_obj_create(p);
    w_flatten(list);
    lv_obj_set_size(list, U * 37, LV_PCT(100));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 2, 0);

    lv_obj_t* cb = lv_obj_create(list);
    w_flatten(cb);
    lv_obj_set_size(cb, LV_PCT(100), U * 5);
    lv_obj_set_flex_flow(cb, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cb, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_crumb = w_label(cb, "/sd", &lv_font_montserrat_12, C_DIM);
    lv_obj_set_flex_grow(s_crumb, 1);
    lv_obj_t* su = w_button(cb, LV_SYMBOL_UP, nullptr, C_RAISE, C_DIM, scroll_cb, (void*)(intptr_t)-1);
    lv_obj_set_size(su, U * 5, LV_PCT(100));
    lv_obj_t* sd = w_button(cb, LV_SYMBOL_DOWN, nullptr, C_RAISE, C_DIM, scroll_cb, (void*)(intptr_t)1);
    lv_obj_set_size(sd, U * 5, LV_PCT(100));

    for (int r = 0; r < ROWS; r++) {
        lv_obj_t* row = lv_btn_create(list);
        lv_obj_set_size(row, LV_PCT(100), U * 8);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, C_RAISE, 0);
        lv_obj_set_style_border_color(row, C_LINE, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_pad_left(row, U + 2, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void*)(intptr_t)r);
        s_row[r] = row;

        s_row_name[r] = w_label(row, "", &lv_font_montserrat_14, C_TEXT);
        lv_obj_set_flex_grow(s_row_name[r], 1);
        s_row_size[r] = w_label(row, "", &lv_font_montserrat_10, C_DIM);
        lv_obj_set_style_pad_right(s_row_size[r], U, 0);
    }

    // --- toolpath preview, tap to expand ---
    lv_obj_t* preview = lv_btn_create(p);
    lv_obj_set_size(preview, U * 37, LV_PCT(100));
    lv_obj_set_style_bg_color(preview, C_SURFACE, 0);
    lv_obj_set_style_border_color(preview, C_LINE, 0);
    lv_obj_set_style_border_width(preview, 1, 0);
    lv_obj_set_style_radius(preview, 3, 0);
    lv_obj_set_style_shadow_width(preview, 0, 0);
    lv_obj_add_event_cb(preview, expand_cb, LV_EVENT_CLICKED, nullptr);
    s_preview_lbl = w_label(preview, "TOOLPATH\ntap to expand", &lv_font_montserrat_12, C_DIM);
    lv_obj_set_style_text_align(s_preview_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_preview_lbl);

    // --- actions ---
    lv_obj_t* acts = lv_obj_create(p);
    w_flatten(acts);
    lv_obj_set_flex_grow(acts, 1);
    lv_obj_set_height(acts, LV_PCT(100));
    lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(acts, U, 0);

    lv_obj_t* run = w_button(acts, "RUN", "$SD/Run", C_OK, C_BG, run_cb, nullptr);
    lv_obj_set_size(run, LV_PCT(100), U * 13);
    lv_obj_t* dry = w_button(acts, "DRY", "$C", C_RAISE, C_TEXT, dry_cb, nullptr);
    lv_obj_set_size(dry, LV_PCT(100), U * 7);
    lv_obj_t* rel = w_button(acts, "RELOAD", nullptr, C_RAISE, C_TEXT, reload_cb, nullptr);
    lv_obj_set_size(rel, LV_PCT(100), U * 7);
}

void page_run_tick() {
    if (s_redraw_pending && !pathload_busy()) {
        s_redraw_pending = false;
        toolpath_redraw();
        char b[48];
        snprintf(b, sizeof(b), "TOOLPATH\n%u points\ntap to expand", (unsigned)gpath_count());
        lv_label_set_text(s_preview_lbl, b);
    }

    if (filelist_dirty_take()) s_list_dirty = true;
    if (!s_list_dirty) return;
    s_list_dirty = false;

    lv_label_set_text(s_crumb, filelist_path());
    for (int r = 0; r < ROWS; r++) {
        const fl_entry_t* e = filelist_at((size_t)(s_scroll + r));
        if (!e) {
            lv_label_set_text(s_row_name[r], "");
            lv_label_set_text(s_row_size[r], "");
            lv_obj_set_style_bg_color(s_row[r], C_SURFACE, 0);
            lv_obj_set_style_border_color(s_row[r], C_LINE, 0);
            continue;
        }
        lv_label_set_text(s_row_name[r], e->name);
        char b[20];
        if (e->is_dir)                    snprintf(b, sizeof(b), "DIR");
        else if (e->size >= 1024 * 1024)  snprintf(b, sizeof(b), "%.1f MB", e->size / 1048576.0);
        else                              snprintf(b, sizeof(b), "%u KB", (unsigned)(e->size / 1024));
        lv_label_set_text(s_row_size[r], b);

        bool sel = ((s_scroll + r) == s_sel) && !e->is_dir;
        lv_obj_set_style_bg_color(s_row[r], sel ? C_SEL_BG : C_RAISE, 0);
        lv_obj_set_style_border_color(s_row[r], sel ? C_ACCENT : C_LINE, 0);
    }
}
