#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <stdio.h>

// The tool rack, driven through FluidNC's atc_manual:
//   CURRENT  M61 Q0 then M61 Q<n>: this tool is in the spindle and Z was zeroed
//            with it. Clears the old TLO and the toolsetter reference, no motion.
//   LOAD     M6 T<n>: measures the outgoing tool if no reference yet, goes to
//            the change position and pauses (the tool-change card takes over),
//            then measures the new one and sets the TLO.
//   UNLOAD   M6 T0: goes to the change position and clears the TLO.
// $G after each refreshes the T word that mySelectedTool comes from.

// How many tool buttons the rack shows: an operator choice, not a layout
// one. The grid below is sized from the page, with one cell for the readout.
static const int TOOL_SLOTS = 13;
static const int COLS       = 7;
static const int ROWS       = (TOOL_SLOTS + 1 + COLS - 1) / COLS;

static lv_obj_t* s_btn[TOOL_SLOTS];
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
        case A_CURRENT: send_line("M61 Q0"); send_linef("M61 Q%d", s_tool);   break;
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
    w_gate(s_act[A_UNLOAD],  idle && unl,  !idle ? busy : in == 0 ? "spindle is empty" : why_unl);
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

void page_tools_build(lv_obj_t* p) {
    static lv_coord_t cols[COLS + 1], rows[ROWS + 1];
    for (int i = 0; i < COLS; i++) cols[i] = LV_GRID_FR(1);
    for (int i = 0; i < ROWS; i++) rows[i] = LV_GRID_FR(1);
    cols[COLS] = rows[ROWS] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(p, cols, rows);
    lv_obj_set_layout(p, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(p, U, 0);
    lv_obj_set_style_pad_row(p, U, 0);

    for (int i = 0; i < TOOL_SLOTS; i++) {
        char b[8];
        snprintf(b, sizeof(b), "T%d", i + 1);
        s_btn[i] = w_button(p, b, nullptr, C_SURFACE, C_TEXT, tool_cb, (void*)(intptr_t)(i + 1));
        lv_obj_set_grid_cell(s_btn[i], LV_GRID_ALIGN_STRETCH, i % COLS, 1, LV_GRID_ALIGN_STRETCH, i / COLS, 1);
    }

    lv_obj_t* now = w_panel(p, C_BG);
    lv_obj_set_grid_cell(now, LV_GRID_ALIGN_STRETCH, TOOL_SLOTS % COLS, 1,
                         LV_GRID_ALIGN_STRETCH, TOOL_SLOTS / COLS, 1);
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
    if (s_shown) snprintf(b, sizeof(b), "T%u", (unsigned)s_shown);
    else         snprintf(b, sizeof(b), "--");
    lv_label_set_text(s_now, b);
    for (int i = 0; i < TOOL_SLOTS; i++) {
        const bool on = (uint32_t)(i + 1) == s_shown;
        lv_obj_set_style_bg_color(s_btn[i], on ? C_ACCENT : C_SURFACE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(lv_obj_get_child(s_btn[i], 0), 0), on ? C_BG : C_TEXT, 0);
    }
}
