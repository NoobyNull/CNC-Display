#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/view_expanded.h"
#include "model/FluidNCModel.h"
#include "model/fnc_settings.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <string.h>

// Settings rows that fit the list, derived from the panel. Fixed at 5 before,
// which is what the CrowPanel's 240px page held; the 7B fits 8. There are ~150
// settings to page through, so every extra row is one less scroll.
static const int ROWS = (CONTENT_H - U * 2 + 2) / (U * 5 + 2);

static lv_obj_t* s_row[ROWS];
static lv_obj_t* s_name[ROWS];
static lv_obj_t* s_val[ROWS];
static lv_obj_t* s_count_lbl;
static lv_obj_t* s_edit_ta;
static lv_obj_t* s_kb;
static int s_scroll = 0;
static int s_sel = -1;

static void refresh();

static void row_cb(lv_event_t* e) {
    int r = (int)(intptr_t)lv_event_get_user_data(e);
    const fnc_setting_t* st = fncset_at((size_t)(s_scroll + r));
    if (!st) return;
    s_sel = s_scroll + r;
    lv_textarea_set_text(s_edit_ta, st->value);
    refresh();
}

static void scroll_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int max = (int)fncset_count() - ROWS;
    if (max < 0) max = 0;
    s_scroll += dir * ROWS;          // page at a time; there are ~150 of these
    if (s_scroll < 0) s_scroll = 0;
    if (s_scroll > max) s_scroll = max;
    refresh();
}

static void all_cb(lv_event_t*)     { s_scroll = 0; s_sel = -1; lv_textarea_set_text(s_edit_ta, ""); fncset_request_all(); }
static void changed_cb(lv_event_t*) { s_scroll = 0; s_sel = -1; lv_textarea_set_text(s_edit_ta, ""); fncset_request_changed(); }

static void edit_focus_cb(lv_event_t*) {
    // With a setting selected this edits its value; with nothing selected it is
    // a free command line — anything typed goes to the controller verbatim.
    lv_keyboard_set_textarea(s_kb, s_edit_ta);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_kb);
}

// Writing sends "$name=value". FluidNC validates and answers ok or error:N,
// which lands in the console view — so a rejected value is visible rather than
// silently ignored. Re-reading afterwards confirms what actually stuck.
static void kb_done_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb, nullptr);
    if (code != LV_EVENT_READY) return;

    const fnc_setting_t* st = fncset_at((size_t)s_sel);
    if (st) {
        fncset_write(st->name, lv_textarea_get_text(s_edit_ta));
        return;
    }
    // No selection: the box is a command line. Replies are plain lines, so
    // show the console to make ok/error visible.
    const char* t = lv_textarea_get_text(s_edit_ta);
    if (t && *t) {
        send_line(t);
        expanded_open(EXPANDED_CONSOLE);
    }
}

static void diag_cmd_cb(lv_event_t* e) {
    send_line((const char*)lv_event_get_user_data(e));
    expanded_open(EXPANDED_CONSOLE);      // replies are plain lines; show them
}

static void refresh() {
    char b[24];
    snprintf(b, sizeof(b), "%u settings", (unsigned)fncset_count());
    lv_label_set_text(s_count_lbl, b);

    for (int r = 0; r < ROWS; r++) {
        const fnc_setting_t* st = fncset_at((size_t)(s_scroll + r));
        lv_label_set_text(s_name[r], st ? st->name : "");
        lv_label_set_text(s_val[r],  st ? st->value : "");
        bool sel = st && ((s_scroll + r) == s_sel);
        lv_obj_set_style_bg_color(s_row[r], sel ? C_SEL_BG : C_SURFACE, 0);
        lv_obj_set_style_border_color(s_row[r], sel ? C_ACCENT : C_LINE, 0);
    }
}

void page_diag_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p, U, 0);

    // --- settings list ---
    lv_obj_t* list = lv_obj_create(p);
    w_flatten(list);
    lv_obj_set_size(list, U * 56, LV_PCT(100));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 2, 0);

    for (int r = 0; r < ROWS; r++) {
        lv_obj_t* row = lv_btn_create(list);
        lv_obj_set_size(row, LV_PCT(100), U * 5);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, C_SURFACE, 0);
        lv_obj_set_style_border_color(row, C_LINE, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_pad_left(row, U, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void*)(intptr_t)r);
        s_row[r] = row;

        s_name[r] = w_label(row, "", &lv_font_montserrat_12, C_TEXT);
        lv_obj_set_flex_grow(s_name[r], 1);
        s_val[r] = w_label(row, "", &lv_font_montserrat_12, C_ACCENT);
        lv_obj_set_style_pad_right(s_val[r], U, 0);
    }

    // --- right column ---
    lv_obj_t* right = lv_obj_create(p);
    w_flatten(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, U, 0);

    lv_obj_t* nav = lv_obj_create(right);
    w_flatten(nav);
    lv_obj_set_size(nav, LV_PCT(100), U * 5);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(nav, U, 0);
    lv_obj_t* up = w_button(nav, LV_SYMBOL_UP, nullptr, C_RAISE, C_DIM, scroll_cb, (void*)(intptr_t)-1);
    lv_obj_set_size(up, U * 8, LV_PCT(100));
    lv_obj_t* dn = w_button(nav, LV_SYMBOL_DOWN, nullptr, C_RAISE, C_DIM, scroll_cb, (void*)(intptr_t)1);
    lv_obj_set_size(dn, U * 8, LV_PCT(100));
    s_count_lbl = w_label(nav, "0 settings", &lv_font_montserrat_10, C_DIM);
    lv_obj_set_flex_grow(s_count_lbl, 1);

    s_edit_ta = lv_textarea_create(right);
    lv_textarea_set_one_line(s_edit_ta, true);
    lv_textarea_set_placeholder_text(s_edit_ta, "setting value / $ command");
    lv_obj_set_size(s_edit_ta, LV_PCT(100), U * 6);
    lv_obj_set_style_bg_color(s_edit_ta, C_SURFACE, 0);
    lv_obj_set_style_text_color(s_edit_ta, C_TEXT, 0);
    lv_obj_set_style_border_color(s_edit_ta, C_LINE, 0);
    lv_obj_add_event_cb(s_edit_ta, edit_focus_cb, LV_EVENT_CLICKED, nullptr);

    // The two button rows take the slack. The nav bar and the one-line editor
    // have a natural size and look wrong stretched; taller command buttons are
    // a straight win on a panel operated with a gloved hand.
    lv_obj_t* btns = lv_obj_create(right);
    w_flatten(btns);
    lv_obj_set_width(btns, LV_PCT(100));
    lv_obj_set_flex_grow(btns, 1);
    lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btns, U, 0);
    lv_obj_t* a = w_button(btns, "ALL", "$S", C_RAISE, C_TEXT, all_cb, nullptr);
    lv_obj_set_flex_grow(a, 1); lv_obj_set_height(a, LV_PCT(100));
    lv_obj_t* c = w_button(btns, "CHANGED", "$SC", C_RAISE, C_TEXT, changed_cb, nullptr);
    lv_obj_set_flex_grow(c, 1); lv_obj_set_height(c, LV_PCT(100));

    lv_obj_t* diag = lv_obj_create(right);
    w_flatten(diag);
    lv_obj_set_width(diag, LV_PCT(100));
    lv_obj_set_flex_grow(diag, 1);
    lv_obj_set_flex_flow(diag, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(diag, U, 0);
    static const char* CMDS[3]  = { "$Limits/Show", "$CI", "$GD" };
    static const char* LABEL[3] = { "LIMITS", "CHANNELS", "GPIO" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* b = w_button(diag, LABEL[i], nullptr, C_RAISE, C_TEXT,
                               diag_cmd_cb, (void*)CMDS[i]);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_height(b, LV_PCT(100));
    }

    // $CD=<file> dumps the LIVE config tree over the file the controller boots
    // from — the persistence FluidNC has but never documents. It rewrites the
    // whole file (regenerated, comments gone), so it takes a hold, not a tap.
    lv_obj_t* sv = w_button(diag, "SAVE CFG", "hold", C_RAISE, C_WARN, nullptr, nullptr);
    lv_obj_set_flex_grow(sv, 1);
    lv_obj_set_height(sv, LV_PCT(100));
    lv_obj_add_event_cb(sv, [](lv_event_t*) {
        send_line("$CD=config.yaml");
        expanded_open(EXPANDED_CONSOLE);
    }, LV_EVENT_LONG_PRESSED, nullptr);

    s_kb = w_keyboard();
    lv_obj_add_event_cb(s_kb, kb_done_cb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_kb, kb_done_cb, LV_EVENT_CANCEL, nullptr);
}

void page_diag_tick() {
    if (fncset_dirty_take()) refresh();
}
