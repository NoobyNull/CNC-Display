#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <stdio.h>

// The tool rack (T0 by hand, T1-T5 in the OpenATC, T6+ off-rack), driven
// through FluidNC's atc_manual until the OpenATC macro takes over M6:
//   CURRENT  M61 Q0 then M61 Q<n>: this tool is in the spindle and Z was zeroed
//            with it. Clears the old TLO and the toolsetter reference, no motion.
//   LOAD     M6 T<n>: measures the outgoing tool if no reference yet, goes to
//            the change position and pauses (the tool-change card takes over),
//            then measures the new one and sets the TLO.
//   UNLOAD   M6 T0: goes to the change position and clears the TLO.
// $G after each refreshes the T word that mySelectedTool comes from.

// The machine's tools: T0 is whatever is fitted by hand, T1..RACK_POCKETS live
// in the OpenATC rack along the far (high-Y) edge, pocket n holding Tn, and the
// rest up to TOOL_MAX are numbered tools changed by hand. Both counts are the
// hardware's, not the layout's: the grid below is sized from them.
static const int RACK_POCKETS = 5;
static const int TOOL_MAX     = 12;
static const int COLS         = RACK_POCKETS + 2;   // T0, the rack, the readout
static const int OFF_RACK     = TOOL_MAX - RACK_POCKETS;
static const int OFF_ROWS     = (OFF_RACK + COLS - 1) / COLS;

static bool in_rack(int t) { return t >= 1 && t <= RACK_POCKETS; }

static lv_obj_t* s_btn[TOOL_MAX + 1];      // indexed by tool number, T0 included
static lv_obj_t* s_now;            // "IN SPINDLE T4"
static lv_obj_t* s_pop;
static lv_obj_t* s_act[3];
static int       s_tool;           // the tool the open card is about
static uint32_t  s_shown = (uint32_t)-1;

static void pop_close() {
    if (!s_pop) return;
    lv_obj_del(s_pop);
    s_pop = nullptr;
}

enum { A_LOAD, A_CURRENT, A_UNLOAD };

static void act_cb(lv_event_t* e) {
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
        case A_LOAD:    send_linef("M6 T%d", s_tool);                         break;
        case A_CURRENT: send_line("M61 Q0"); if (s_tool) send_linef("M61 Q%d", s_tool); break;
        case A_UNLOAD:  send_line("M6 T0");                                   break;
    }
    send_line("$G");
    pop_close();
}

// Only what makes sense for what is in the spindle is offered; the rest says
// why. A machine that is not Idle offers nothing.
static void gate_actions() {
    if (!s_pop) return;
    static char why_cur[40], why_unl[40];
    const int  in    = (int)mySelectedTool;   // 0 = empty
    const bool idle  = state == Idle;
    const char* busy = w_locked() ? LOCK_REASON : "machine busy";
    snprintf(why_cur, sizeof(why_cur), "T%d is loaded: load or unload", in);
    snprintf(why_unl, sizeof(why_unl), "T%d is loaded, not T%d", in, s_tool);

    const bool load = in != s_tool;
    const bool cur  = in == 0 || in == s_tool;
    const bool unl  = in != 0 && in == s_tool;
    w_gate(s_act[A_LOAD],    idle && load, !idle ? busy : "already loaded");
    w_gate(s_act[A_CURRENT], idle && cur,  !idle ? busy : why_cur);
    w_gate(s_act[A_UNLOAD],  idle && unl,  !idle ? busy : s_tool == 0 ? "hand tool: just take it out"
                                           : in == 0 ? "spindle is empty" : why_unl);
}

static void tool_cb(lv_event_t* e) {
    s_tool = (int)(intptr_t)lv_event_get_user_data(e);

    // Scrim stops short of the safety bar, like every other card.
    s_pop = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_pop);
    lv_obj_set_size(s_pop, SCREEN_W, SCREEN_H - SAFETY_H);
    lv_obj_set_style_bg_color(s_pop, C_BG, 0);
    lv_obj_set_style_bg_opa(s_pop, LV_OPA_80, 0);
    lv_obj_add_flag(s_pop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_pop, [](lv_event_t*) { pop_close(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* card = w_panel(s_pop, C_SURFACE);
    lv_obj_set_size(card, CONTENT_W - U * 4, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_CENTER, RAIL_W / 2, 0);
    lv_obj_set_style_pad_all(card, U * 2, 0);
    lv_obj_set_style_pad_row(card, U, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);   // taps on the card do not close it

    char b[16];
    snprintf(b, sizeof(b), "T%d", s_tool);
    w_label(card, b, &lv_font_montserrat_40, C_TEXT);
    char where[64];
    if (s_tool == 0)          snprintf(where, sizeof(where), "Manual tool - fitted by hand, not tracked");
    else if (in_rack(s_tool)) snprintf(where, sizeof(where), "Rack pocket %d - OpenATC, far end of the table", s_tool);
    else                      snprintf(where, sizeof(where), "Not in the rack - changed by hand");
    w_label(card, where, &lv_font_montserrat_14, C_DIM);

    lv_obj_t* row = lv_obj_create(card);
    w_flatten(row);
    lv_obj_set_size(row, LV_PCT(100), TOUCH_HOLD + U * 2);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, U, 0);

    static const char* const LBL[3] = { "LOAD", "CURRENT", "UNLOAD" };
    static const char* const SUB[3] = {
        "measure, go to change, swap", "in spindle, Z zeroed with it", "go to change, clear TLO" };
    const lv_color_t BG[3] = { C_ACCENT, C_OK, C_RAISE };
    const lv_color_t FG[3] = { C_BG, C_BG, C_TEXT };
    for (int k = 0; k < 3; k++) {
        s_act[k] = w_button(row, LBL[k], SUB[k], BG[k], FG[k], act_cb, (void*)(intptr_t)k);
        lv_obj_set_flex_grow(s_act[k], 1);
        lv_obj_set_height(s_act[k], LV_PCT(100));
    }
    gate_actions();
}

static lv_obj_t* tool_button(lv_obj_t* p, int t, const char* sub, int col, int row) {
    char b[8];
    snprintf(b, sizeof(b), "T%d", t);
    lv_obj_t* btn = w_button(p, b, sub, C_SURFACE, C_TEXT, tool_cb, (void*)(intptr_t)t);
    lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
    return btn;
}

void page_tools_build(lv_obj_t* p) {
    // Row 0 names the groups, row 1 is T0 + the rack as it sits on the machine
    // + the readout, then the off-rack tools fill whole rows below.
    static lv_coord_t cols[COLS + 1], rows[2 + OFF_ROWS + 1];
    for (int i = 0; i < COLS; i++) cols[i] = LV_GRID_FR(1);
    rows[0] = LV_GRID_CONTENT;
    for (int i = 1; i < 2 + OFF_ROWS; i++) rows[i] = LV_GRID_FR(1);
    cols[COLS] = rows[2 + OFF_ROWS] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(p, cols, rows);
    lv_obj_set_layout(p, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(p, U, 0);
    lv_obj_set_style_pad_row(p, U, 0);

    lv_obj_t* h = w_label(p, "MANUAL", &lv_font_montserrat_10, C_DIM);
    lv_obj_set_grid_cell(h, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    h = w_label(p, "OPENATC RACK  -  far end of the table", &lv_font_montserrat_10, C_ACCENT);
    lv_obj_set_grid_cell(h, LV_GRID_ALIGN_CENTER, 1, RACK_POCKETS, LV_GRID_ALIGN_CENTER, 0, 1);

    s_btn[0] = tool_button(p, 0, "hand", 0, 1);
    for (int t = 1; t <= RACK_POCKETS; t++) {
        char sub[12];
        snprintf(sub, sizeof(sub), "pocket %d", t);
        s_btn[t] = tool_button(p, t, sub, t, 1);
        // The rack reads as one strip: a pocket outline rather than a plain key.
        lv_obj_set_style_border_color(s_btn[t], C_ACCENT, 0);
        lv_obj_set_style_border_opa(s_btn[t], LV_OPA_50, 0);
    }
    for (int k = 0; k < OFF_RACK; k++) {
        const int t = RACK_POCKETS + 1 + k;
        s_btn[t] = tool_button(p, t, nullptr, k % COLS, 2 + k / COLS);
    }

    lv_obj_t* now = w_panel(p, C_BG);
    lv_obj_set_grid_cell(now, LV_GRID_ALIGN_STRETCH, COLS - 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_t* k = w_label(now, "IN SPINDLE", &lv_font_montserrat_10, C_DIM);
    lv_obj_align(k, LV_ALIGN_TOP_MID, 0, U);
    s_now = w_label(now, "--", &lv_font_montserrat_28, C_ACCENT);
    lv_obj_align(s_now, LV_ALIGN_CENTER, 0, U);
}

void page_tools_tick() {
    static bool asked = false;
    if (!asked && state != Disconnected) { send_line("$G"); asked = true; }   // T word, once
    if (state == Disconnected) asked = false;

    // Back at rest: a change (ours, or a job's) may have moved the T word.
    static state_t was = Disconnected;
    if (state != was) {
        if (state == Idle && was != Disconnected) send_line("$G");
        was = state;
        gate_actions();
    }

    if (s_shown == mySelectedTool) return;
    s_shown = mySelectedTool;
    gate_actions();
    char b[8];
    snprintf(b, sizeof(b), "T%u", (unsigned)s_shown);   // T0 = the hand tool
    lv_label_set_text(s_now, b);
    for (int t = 0; t <= TOOL_MAX; t++) {
        const bool on = (uint32_t)t == s_shown;
        lv_obj_set_style_bg_color(s_btn[t], on ? C_ACCENT : C_SURFACE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(lv_obj_get_child(s_btn[t], 0), 0), on ? C_BG : C_TEXT, 0);
    }
}
