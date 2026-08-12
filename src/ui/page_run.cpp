#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/view_expanded.h"
#include "model/filelist.h"
#include "model/FluidNCModel.h"
#include "model/gcode_path.h"
#include "model/path_loader.h"
#include "model/file_upload.h"
#include "hw/sdcard.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <string.h>

// File rows that fit the list, derived from the panel. This was fixed at 3,
// which is exactly what the CrowPanel's 240px page held — the formula returns 3
// for that screen and 4 for the 7B, so the list grows with the panel instead of
// leaving a band of background under the last row.
// Layout: the control bar, then the rows, all separated by a 2px pad_row.
static const int ROWS = (CONTENT_H - U * 2 - U * 5) / (U * 8 + 2);

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

// Which card we are browsing. The controller's SD is normally the right source,
// but when the controller lives in a sealed box you cannot reach its card, so
// the panel's own slot becomes the source and files are pushed across with $XR.
static bool s_local = false;
static lv_obj_t* s_src_btn;
static lv_obj_t* s_src_lbl;

static size_t src_count()            { return s_local ? sd_count() : filelist_count(); }
static const char* src_name(size_t i) {
    if (s_local) { const sd_entry_t* e = sd_at(i); return e ? e->name : nullptr; }
    const fl_entry_t* e = filelist_at(i); return e ? e->name : nullptr;
}
static bool src_is_dir(size_t i) {
    if (s_local) { const sd_entry_t* e = sd_at(i); return e && e->is_dir; }
    const fl_entry_t* e = filelist_at(i); return e && e->is_dir;
}
static uint32_t src_size(size_t i) {
    if (s_local) { const sd_entry_t* e = sd_at(i); return e ? e->size : 0; }
    const fl_entry_t* e = filelist_at(i); return e ? e->size : 0;
}

static void src_toggle_cb(lv_event_t*) {
    s_local = !s_local;
    if (s_local) sd_list("/");
    else         filelist_request();
    lv_label_set_text(s_src_lbl, s_local ? "PANEL SD" : "CONTROLLER");
    s_scroll = 0; s_sel = -1; s_selected[0] = 0;
    s_list_dirty = true;
}

// Copy the selected local file to the controller, then it runs like any other.
static void upload_cb(lv_event_t*) {
    if (!s_local || !s_selected[0]) return;
    char local[160], remote[160];
    snprintf(local,  sizeof(local),  "/%s", s_selected);
    snprintf(remote, sizeof(remote), "/sd/%s", s_selected);
    upload_start(local, remote);
    s_list_dirty = true;
}

static void row_cb(lv_event_t* e) {
    int r = (int)(intptr_t)lv_event_get_user_data(e);
    size_t idx = (size_t)(s_scroll + r);
    const char* nm = src_name(idx);
    if (!nm) return;

    if (src_is_dir(idx)) {
        if (!s_local) { filelist_enter(nm); s_scroll = 0; s_sel = -1; s_list_dirty = true; }
        return;
    }
    s_sel = s_scroll + r;
    strncpy(s_selected, nm, sizeof(s_selected) - 1);
    s_selected[sizeof(s_selected) - 1] = 0;
    s_list_dirty = true;

    if (!s_local) {
        char full[192];
        snprintf(full, sizeof(full), "%s/%s", filelist_path(), s_selected);
        if (pathload_start(full)) s_redraw_pending = true;
    }
}

static void scroll_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int n = s_scroll + dir;
    int max = (int)src_count() - ROWS;
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
static void reload_cb(lv_event_t*) {
    if (s_local) sd_list("/"); else filelist_request();
    s_list_dirty = true;
}
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
    s_src_btn = w_button(cb, "CONTROLLER", nullptr, C_RAISE, C_ACCENT, src_toggle_cb, nullptr);
    lv_obj_set_size(s_src_btn, U * 15, LV_PCT(100));
    s_src_lbl = lv_obj_get_child(lv_obj_get_child(s_src_btn, 0), 0);
    s_crumb = w_label(cb, "/sd", &lv_font_montserrat_10, C_DIM);
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

    // RUN absorbs the slack rather than every button growing equally: it is the
    // primary action on the page and the size hierarchy over DRY/SEND/RELOAD is
    // deliberate, so the extra height reinforces it instead of flattening it.
    lv_obj_t* run = w_button(acts, "RUN", "$SD/Run", C_OK, C_BG, run_cb, nullptr);
    lv_obj_set_width(run, LV_PCT(100));
    lv_obj_set_flex_grow(run, 1);
    lv_obj_t* dry = w_button(acts, "DRY", "$C", C_RAISE, C_TEXT, dry_cb, nullptr);
    lv_obj_set_size(dry, LV_PCT(100), U * 6);
    lv_obj_t* snd = w_button(acts, "SEND", "$XR", C_ACCENT, C_BG, upload_cb, nullptr);
    lv_obj_set_size(snd, LV_PCT(100), U * 7);
    lv_obj_t* rel = w_button(acts, "RELOAD", nullptr, C_RAISE, C_TEXT, reload_cb, nullptr);
    lv_obj_set_size(rel, LV_PCT(100), U * 7);
}

void page_run_tick() {
    if (upload_busy()) {
        char b[64];
        snprintf(b, sizeof(b), "SENDING\n%d%%\n%s", upload_percent(), upload_status());
        lv_label_set_text(s_preview_lbl, b);
        return;
    }
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

    lv_label_set_text(s_crumb, s_local ? "panel /" : filelist_path());
    for (int r = 0; r < ROWS; r++) {
        size_t idx = (size_t)(s_scroll + r);
        const char* nm = src_name(idx);
        if (!nm) {
            lv_label_set_text(s_row_name[r], "");
            lv_label_set_text(s_row_size[r], "");
            lv_obj_set_style_bg_color(s_row[r], C_SURFACE, 0);
            lv_obj_set_style_border_color(s_row[r], C_LINE, 0);
            continue;
        }
        lv_label_set_text(s_row_name[r], nm);
        char b[20];
        uint32_t sz = src_size(idx);
        if (src_is_dir(idx))         snprintf(b, sizeof(b), "DIR");
        else if (sz >= 1024 * 1024)  snprintf(b, sizeof(b), "%.1f MB", sz / 1048576.0);
        else                         snprintf(b, sizeof(b), "%u KB", (unsigned)(sz / 1024));
        lv_label_set_text(s_row_size[r], b);

        bool sel = ((s_scroll + r) == s_sel) && !src_is_dir(idx);
        lv_obj_set_style_bg_color(s_row[r], sel ? C_SEL_BG : C_RAISE, 0);
        lv_obj_set_style_border_color(s_row[r], sel ? C_ACCENT : C_LINE, 0);
    }
}
