#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/offsets.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <string.h>

// Work-system rows that fit the table, derived from the panel. Fixed at 6
// before, which is what the CrowPanel's 240px page held; the 7B fits 9, enough
// to show G54-G59 without scrolling at all.
// Layout: the column header, then the rows, separated by a 2px pad_row.
static const int VISIBLE_ROWS = (CONTENT_H - U * 2 - U * 3) / (U * 4 + 2);

static lv_obj_t* s_rowobj[VISIBLE_ROWS];
static lv_obj_t* s_name[VISIBLE_ROWS];
static lv_obj_t* s_cell[VISIBLE_ROWS][3];
static int s_sel = 0;

static void row_pick_cb(lv_event_t* e) {
    s_sel = (int)(intptr_t)lv_event_get_user_data(e);
    page_offsets_tick();
}

// Every action on this page writes persistent machine state, so every button
// shows the G-code it sends. If it does something unintended, you can see what
// it did.
static void set_here_cb(lv_event_t*) {
    if (s_sel > 5) return;                    // only G54..G59 are settable this way
    send_linef("G10 L20 P%d X0 Y0 Z0", s_sel + 1);
    send_line("$#");
}

static void goto_origin_cb(lv_event_t*) {
    if (s_sel > 5) return;
    send_linef("G%d G0 X0 Y0", 54 + s_sel);
}

static void clear_g92_cb(lv_event_t*) {
    send_line("G92.1");
    send_line("$#");
}

static void activate_cb(lv_event_t*) {
    if (s_sel > 5) return;
    send_linef("G%d", 54 + s_sel);
    send_line("$#");
}

void page_offsets_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p, U, 0);

    lv_obj_t* tab = lv_obj_create(p);
    w_flatten(tab);
    lv_obj_set_size(tab, U * 60, LV_PCT(100));
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, 2, 0);

    lv_obj_t* head = lv_obj_create(tab);
    w_flatten(head);
    lv_obj_set_size(head, LV_PCT(100), U * 3);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_t* h0 = w_label(head, "SYSTEM", &lv_font_montserrat_10, C_DIM);
    lv_obj_set_width(h0, U * 8);
    static const char* HX[3] = { "X", "Y", "Z" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* h = w_label(head, HX[i], &lv_font_montserrat_10, C_DIM);
        lv_obj_set_flex_grow(h, 1);
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_RIGHT, 0);
    }

    for (int r = 0; r < VISIBLE_ROWS; r++) {
        lv_obj_t* row = lv_btn_create(tab);
        lv_obj_set_size(row, LV_PCT(100), U * 4);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, C_SURFACE, 0);
        lv_obj_set_style_border_color(row, C_LINE, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_pad_left(row, U + 2, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, row_pick_cb, LV_EVENT_CLICKED, (void*)(intptr_t)r);
        s_rowobj[r] = row;

        s_name[r] = w_label(row, "---", &lv_font_montserrat_14, C_DIM);
        lv_obj_set_width(s_name[r], U * 8);

        for (int c = 0; c < 3; c++) {
            s_cell[r][c] = w_label(row, "---", &lv_font_montserrat_14, C_TEXT);
            lv_obj_set_flex_grow(s_cell[r][c], 1);
            lv_obj_set_style_text_align(s_cell[r][c], LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_pad_right(s_cell[r][c], U + 2, 0);
        }
    }

    lv_obj_t* acts = lv_obj_create(p);
    w_flatten(acts);
    lv_obj_set_flex_grow(acts, 1);
    lv_obj_set_height(acts, LV_PCT(100));
    lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(acts, U, 0);

    struct { const char* t; const char* s; lv_event_cb_t cb; lv_color_t bg; lv_color_t fg; } A[4] = {
        { "SET FROM HERE", "G10 L20 Pn",   set_here_cb,    C_ACCENT, C_BG   },
        { "GO TO ORIGIN",  "Gnn G0 X0 Y0", goto_origin_cb, C_RAISE,  C_TEXT },
        { "CLEAR G92",     "G92.1",        clear_g92_cb,   C_RAISE,  C_TEXT },
        { "ACTIVATE",      "Gnn",          activate_cb,    C_RAISE,  C_TEXT },
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* b = w_button(acts, A[i].t, A[i].s, A[i].bg, A[i].fg, A[i].cb, nullptr);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_flex_grow(b, 1);
    }
}

void page_offsets_tick() {
    for (int r = 0; r < VISIBLE_ROWS; r++) {
        const wcs_t* w = offsets_row(r);
        if (!w) continue;
        lv_label_set_text(s_name[r], w->name);
        for (int c = 0; c < 3; c++) {
            char b[20];
            if (w->valid) snprintf(b, sizeof(b), "%.3f", (double)w->axis[c]);
            else          snprintf(b, sizeof(b), "---");
            lv_label_set_text(s_cell[r][c], b);
        }
        bool sel = (r == s_sel);
        lv_obj_set_style_bg_color(s_rowobj[r], sel ? C_SEL_BG : C_SURFACE, 0);
        lv_obj_set_style_border_color(s_rowobj[r], sel ? C_ACCENT : C_LINE, 0);
        lv_obj_set_style_text_color(s_name[r], sel ? C_ACCENT : C_DIM, 0);
    }
}
