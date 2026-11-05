#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "model/homed.h"
#include "model/caps.h"
#include "model/toolchange.h"
#include "ui/spindle_card.h"
#include "GrblParserC.h"
#include <stdio.h>

// What a tool's card offers depends on what M6 really does on this controller
// (caps_atc(), read from its config), because the card must not promise a rack
// the controller is not running:
//
// OpenATC macro (m6_macro, CNC-Wiring/openatc_m6.nc): LOAD or CANCEL. LOAD is
//   M6 T<n> and FluidNC does the rest, the card spelling out the plan first.
//
// atc_manual: every change is by hand, and it needs to be told what is in:
//   CURRENT  M61 Q0, M61 Q<n>: this tool is in and Z was zeroed with it. No motion.
//   LOAD     M6 T<n>. From a known tool: measures it if there is no reference
//            yet, goes to the change position, pauses, measures the new one.
//            From T0: goes to the change position and that is ALL -- no pause,
//            no measuring. Fit the tool, zero Z, then CURRENT.
//   UNLOAD   M6 T0: goes to the change position and clears the TLO.

// One button per OpenATC cartridge: pocket n holds Tn, stacked on the right-hand
// (high-X) side with pocket 1 nearest the operator. The count is the hardware's,
// not the layout's. Anything else in the spindle -- T0, a number that is not a
// cartridge, or nothing FluidNC knows of -- is one thing: the custom tool, fitted
// and removed by hand.
static const int RACK_POCKETS = 5;

static bool in_rack(int t) { return t >= 1 && t <= RACK_POCKETS; }

static lv_obj_t* s_btn[RACK_POCKETS + 1];  // indexed by tool number; [0] unused
static lv_obj_t* s_now;            // "IN SPINDLE T4"
static lv_obj_t* s_rack_lbl;
static lv_obj_t* s_pop;
static lv_obj_t* s_act[3];          // LOAD, CURRENT, UNLOAD; the last two only by hand
static int       s_tool;           // the tool the open card is about

static void pop_close() {
    if (!s_pop) return;
    lv_obj_del(s_pop);
    s_pop = nullptr;
}

enum { A_LOAD, A_CURRENT, A_UNLOAD };

static bool by_macro() { return caps_atc() == CAPS_ATC_MACRO; }

// What is in the spindle: a cartridge, TC_EMPTY, 0 for the custom tool, or -1
// for not known. With the macro FluidNC knows, or says it does not (after every
// boot and reset); atc_manual never updates FluidNC's current tool, so there
// the selected tool from $G is the best there is.
static const int UNKNOWN = -1;
static int in_spindle() {
    if (by_macro() && tc_current() < 0) return UNKNOWN;
    const int t = by_macro() ? tc_current() : (int)mySelectedTool;
    return in_rack(t) || (by_macro() && t == TC_EMPTY) ? t : 0;
}

static void refresh_tool() {
    send_line("$G");
    if (by_macro()) send_line(TC_ASK_CURRENT);
}

static void act_cb(lv_event_t* e) {
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
        case A_LOAD:    send_linef("M6 T%d", s_tool);                                   break;
        case A_CURRENT: send_line("M61 Q0"); send_linef("M61 Q%d", s_tool);             break;
        case A_UNLOAD:  send_line("M6 T0");                                             break;
    }
    refresh_tool();
    pop_close();
}

// The same decisions openatc_m6.nc makes, in words, so LOAD is never a surprise.
static void plan_macro(char* out, size_t n, int from, int to) {
    char a[48];
    if (from == TC_EMPTY) { snprintf(out, n, "Pick up T%d from pocket %d  >  measure it", to, to); return; }
    if (in_rack(from)) snprintf(a, sizeof(a), "T%d back to pocket %d", from, from);
    else               snprintf(a, sizeof(a), "remove the custom tool by hand");
    snprintf(out, n, "%s  >  pick up T%d from pocket %d  >  measure it", a, to, to);
}

// And what atc_manual will do. The T0 case is the one that catches people out.
static void plan_manual(char* out, size_t n, int from, int to) {
    if (from <= 0) snprintf(out, n, "LOAD only goes to the change position: it does not stop or measure. "
                                         "Fit T%d, zero Z, then tap CURRENT. If T%d is already in, just tap CURRENT.", to, to);
    else           snprintf(out, n, "Measure T%d if needed  >  change position  >  swap to T%d by hand  >  measure it",
                                 from, to);
}

// Only what makes sense for what is in the spindle is offered; the rest says
// why. Anything that moves needs a machine at rest that knows where it is (the
// change position, rack and toolsetter are machine coordinates).
static void gate_actions() {
    if (!s_pop) return;
    static char why_cur[40], why_unl[40];
    const int   in   = in_spindle();
    const bool  idle = state == Idle;
    const char* busy = w_locked() ? LOCK_REASON : "machine busy";
    const char* move = !idle ? busy : !homed() ? "home first" : nullptr;

    // An unknown spindle is not "the custom tool": the macro would skip the
    // put-back for a cartridge that may still be in the collet.
    const char* load_why = move ? move : in == UNKNOWN ? "spindle unknown: tap IN SPINDLE" : "already in the spindle";
    w_gate(s_act[A_LOAD], !move && in != UNKNOWN && in != s_tool, load_why);
    if (by_macro()) return;

    snprintf(why_cur, sizeof(why_cur), "T%d is loaded: load or unload", in);
    snprintf(why_unl, sizeof(why_unl), "T%d is loaded, not T%d", in, s_tool);
    w_gate(s_act[A_CURRENT], idle && (in == 0 || in == s_tool), !idle ? busy : why_cur);
    w_gate(s_act[A_UNLOAD],  !move && in != 0 && in == s_tool,
           move ? move : in == 0 ? "custom tool: remove by hand" : why_unl);
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
    if (by_macro()) snprintf(where, sizeof(where), "Rack pocket %d of %d - OpenATC, right side, from the front", s_tool, RACK_POCKETS);
    else            snprintf(where, sizeof(where), "Pocket %d - rack not in use yet, changed by hand", s_tool);
    w_label(card, where, &lv_font_montserrat_14, C_DIM);
    if (in_spindle() != s_tool) {
        char p[160];
        if (by_macro()) plan_macro(p, sizeof(p), in_spindle(), s_tool);
        else            plan_manual(p, sizeof(p), (int)mySelectedTool, s_tool);   // atc_manual's own idea of "from"
        lv_obj_t* l = w_label(card, p, &lv_font_montserrat_14, C_TEXT);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }

    lv_obj_t* row = lv_obj_create(card);
    w_flatten(row);
    lv_obj_set_size(row, LV_PCT(100), TOUCH_HOLD + U * 2);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, U, 0);

    char m6[12];
    snprintf(m6, sizeof(m6), "M6 T%d", s_tool);
    s_act[A_LOAD] = w_button(row, "LOAD", m6, C_ACCENT, C_BG, act_cb, (void*)(intptr_t)A_LOAD);
    lv_obj_set_flex_grow(s_act[A_LOAD], 2);
    lv_obj_set_height(s_act[A_LOAD], LV_PCT(100));
    if (!by_macro()) {
        s_act[A_CURRENT] = w_button(row, "CURRENT", "in spindle, Z zeroed with it", C_OK, C_BG,
                                    act_cb, (void*)(intptr_t)A_CURRENT);
        s_act[A_UNLOAD]  = w_button(row, "UNLOAD", "M6 T0", C_RAISE, C_TEXT,
                                    act_cb, (void*)(intptr_t)A_UNLOAD);
        for (int k = A_CURRENT; k <= A_UNLOAD; k++) {
            lv_obj_set_flex_grow(s_act[k], 2);
            lv_obj_set_height(s_act[k], LV_PCT(100));
        }
    }
    lv_obj_t* cancel = w_button(row, "CANCEL", nullptr, C_RAISE, C_TEXT,
                                [](lv_event_t*) { pop_close(); }, nullptr);
    lv_obj_set_flex_grow(cancel, 1);
    lv_obj_set_height(cancel, LV_PCT(100));
    gate_actions();
}

static lv_obj_t* tool_button(lv_obj_t* p, int t, const char* sub) {
    char b[8];
    snprintf(b, sizeof(b), "T%d", t);
    return w_button(p, b, sub, C_SURFACE, C_TEXT, tool_cb, (void*)(intptr_t)t);
}

static lv_obj_t* column(lv_obj_t* p) {
    lv_obj_t* c = lv_obj_create(p);
    w_flatten(c);
    lv_obj_set_height(c, LV_PCT(100));
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, U, 0);
    return c;
}

// Laid out the way the machine looks from the operator's side: the rack is a
// column on the right with pocket 1 at the bottom (front).
void page_tools_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p, U * 2, 0);

    lv_obj_t* left = column(p);
    lv_obj_set_flex_grow(left, 1);
    w_label(left, "IN SPINDLE", &lv_font_montserrat_10, C_DIM);
    lv_obj_t* now = w_panel(left, C_BG);
    lv_obj_set_width(now, LV_PCT(100));
    lv_obj_set_flex_grow(now, 1);
    s_now = w_label(now, "--", &lv_font_montserrat_40, C_ACCENT);
    lv_obj_center(s_now);
    // Tapping it reopens the spindle card if it was skipped.
    lv_obj_add_flag(now, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(now, [](lv_event_t*) { if (in_spindle() == UNKNOWN) spindle_card_ask(); },
                        LV_EVENT_CLICKED, nullptr);

    lv_obj_t* rack = column(p);
    lv_obj_set_flex_grow(rack, 1);
    s_rack_lbl = w_label(rack, "OPENATC RACK  -  back", &lv_font_montserrat_10, C_ACCENT);
    for (int t = RACK_POCKETS; t >= 1; t--) {
        char sub[12];
        snprintf(sub, sizeof(sub), "pocket %d", t);
        s_btn[t] = tool_button(rack, t, sub);
        lv_obj_set_width(s_btn[t], LV_PCT(100));
        lv_obj_set_flex_grow(s_btn[t], 1);
        // The rack reads as one strip: a pocket outline rather than a plain key.
        lv_obj_set_style_border_color(s_btn[t], C_ACCENT, 0);
        lv_obj_set_style_border_opa(s_btn[t], LV_OPA_50, 0);
    }
    w_label(rack, "front", &lv_font_montserrat_10, C_ACCENT);
}

void page_tools_tick() {
    static bool asked = false;
    static int  asked_atc = -1;
    if (state == Disconnected) asked = false;
    if ((!asked || asked_atc != (int)caps_atc()) && state != Disconnected) {   // once, and once the config says
        refresh_tool();
        asked = true;
        asked_atc = (int)caps_atc();
        lv_label_set_text(s_rack_lbl, by_macro() ? "OPENATC RACK  -  back" : "RACK (not in use)  -  back");
    }

    static bool was_homed = false;
    if (homed() != was_homed) { was_homed = homed(); gate_actions(); }

    // Back at rest: a change (ours, or a job's) may have moved the tool.
    static state_t was = Disconnected;
    if (state != was) {
        if (state == Idle && was != Disconnected && was != Jog) refresh_tool();
        was = state;
        gate_actions();
    }

    static int shown = -2;
    const int now = in_spindle();
    if (now == shown) return;
    shown = now;
    gate_actions();
    char b[8];
    if (now == UNKNOWN)       snprintf(b, sizeof(b), "?");
    else if (now == TC_EMPTY) snprintf(b, sizeof(b), "EMPTY");
    else if (now == 0)   snprintf(b, sizeof(b), "CUSTOM");
    else                 snprintf(b, sizeof(b), "T%d", now);
    lv_label_set_text(s_now, b);
    for (int t = 1; t <= RACK_POCKETS; t++) {
        const bool on = t == now;
        lv_obj_set_style_bg_color(s_btn[t], on ? C_ACCENT : C_SURFACE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(lv_obj_get_child(s_btn[t], 0), 0), on ? C_BG : C_TEXT, 0);
    }
}
