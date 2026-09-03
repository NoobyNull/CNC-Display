#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/view_expanded.h"
#include "model/FluidNCModel.h"
#include "model/fnc_settings.h"
#include "model/caps.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// Settings rows that fit the list, derived from the panel. Diag runs
// full-height — the shell hides the DRO strip here — so the budget is the
// content area PLUS the strip's height, not CONTENT_H alone (that undersold
// the page by 4 rows). There are ~150 settings to page through, so every
// extra row is one less scroll.
static const int ROWS = (CONTENT_H + DRO_H - U * 2 + 2) / (U * 5 + 2);

static lv_obj_t* s_row[ROWS];
static lv_obj_t* s_name[ROWS];
static lv_obj_t* s_val[ROWS];
static lv_obj_t* s_count_lbl;
static lv_obj_t* s_edit_ta;
static lv_obj_t* s_kb;
static int s_scroll = 0;
static int s_sel = -1;

// Live limit lamps, one per configured SWITCH, green while it reads active.
// Existence and state both come from $Limits/Show, FluidNC's own limit
// monitor: its preamble ("Limit Axes : ...") lists exactly the motors with
// limit pins, then a sample line arrives every 500ms until a ! stops it — so
// the controller only does monitor work while this page is on screen.
// Lowercase letters are motor0, uppercase motor1 (labelled X2 etc), "P" the
// probe. While the monitor runs, ! only stops it (protocol_do_feedhold
// returns early) — no feed hold happens.
//
// Lamp dark with the switch held = wiring or config; lamp stuck green with
// nothing pressed = inverted logic.
//
// The probe lamp's existence comes from the $S dump (/probe/pin assigned);
// its live state from the Pn: field, which keeps flowing during the monitor.
#define NLAMPS 13   // 6 motor0 + 6 motor1 + probe
static lv_obj_t* s_pin_box[NLAMPS];
static lv_obj_t* s_pin_lbl[NLAMPS];
static bool      s_pin_shown[NLAMPS];
static int       s_pin_state[NLAMPS];
static const char* PIN_NAMES[NLAMPS] = { "X", "Y", "Z", "A", "B", "C",
                                         "X2", "Y2", "Z2", "A2", "B2", "C2", "P" };
static uint16_t  s_cfg_mask  = 0;      // which lamps exist; bit 12 = probe
static uint16_t  s_live_mask = 0;      // which switches read active now
static bool      s_alive     = false;  // $Limits/Show sent, no ! sent since
static bool      s_got_sample = false; // this attempt produced any sample
static int       s_dry       = 0;      // consecutive sampleless attempts
static uint32_t  s_last      = 0;      // last request or sample time, ms

// Motor lamp index for one letter out of a limit_set() string, -1 for
// anything else (spaces, the probe/toolsetter markers).
static int lamp_bit(char c) {
    const char* ax = strchr("XYZABC", toupper((unsigned char)c));
    if (!ax || !c) return -1;
    return (int)(ax - "XYZABC") + (isupper((unsigned char)c) ? 6 : 0);
}

// The documented exit is !. Safe to send blind from here: the monitor only
// runs while the machine sits in Idle/Alarm, so there is no motion to hold.
// s_alive tracks "a monitor may be running on the controller" and is cleared
// ONLY here — never by the watchdog alone. Forgetting a live monitor without
// stopping it leaves the controller ignoring every line command (settings
// writes silently hung: that bug shipped once).
static void mon_stop() {
    if (!s_alive) return;
    s_alive = false;
    fnc_realtime(FeedHold);
}

void page_diag_hidden() {
    mon_stop();
    s_dry = 0;    // a fresh page visit earns fresh attempts
}

// Swallows the monitor's stream so 2 lines a second do not bury the console.
// Returns true when the line belonged to the monitor.
bool page_diag_limits_feed(const char* line) {
    if (!line || !*line) return false;
    if (strncmp(line, "Limit Axes :", 12) == 0) {
        uint16_t m = s_cfg_mask & (uint16_t)(1u << 12);   // keep the probe bit
        for (const char* p = line + 12; *p; p++) {
            int b = lamp_bit(*p);
            if (b >= 0) m |= (uint16_t)(1u << b);
        }
        s_cfg_mask = m;
        return true;
    }
    if (strcmp(line, "Send ! to exit") == 0 ||
        strncmp(line, "Homing Axes :", 13) == 0 ||
        strstr(line, "LimitPins")) {
        return true;   // preamble
    }
    if (line[0] == ':') {          // ": <pos set> <neg set> P T" every 500ms
        uint16_t m = 0;
        for (const char* p = line + 1; *p; p++) {
            int b = lamp_bit(*p);
            if (b >= 0) m |= (uint16_t)(1u << b);
        }
        s_live_mask  = m;
        s_last       = (uint32_t)milliseconds();
        s_got_sample = true;
        s_dry        = 0;
        return true;
    }
    return false;
}

// The probe lamp's existence, from the capability model rather than from a
// direct $S lookup. This used to scan the settings for "/probe/pin", which is
// a FluidNC v3 key: v4's $S carries only the Grbl-compat settings, so that
// lookup never matched and the lamp could never appear. caps.cpp reads the
// config dump, which is where pins actually live on v4.
static void scan_probe_config() {
    uint16_t m = s_cfg_mask & (uint16_t)~(1u << 12);
    if (caps_probe() == CAP_PRESENT) m |= (uint16_t)(1u << 12);
    s_cfg_mask = m;
}

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

// Every deliberate send from this page stops the limit monitor first: while
// it runs the controller processes no line commands, so anything queued would
// sit until the monitor exits. The tick re-arms it right behind the command.
static void mon_stop();

static void all_cb(lv_event_t*)     { mon_stop(); s_scroll = 0; s_sel = -1; lv_textarea_set_text(s_edit_ta, ""); fncset_request_all(); }
static void changed_cb(lv_event_t*) { mon_stop(); s_scroll = 0; s_sel = -1; lv_textarea_set_text(s_edit_ta, ""); fncset_request_changed(); }

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

    mon_stop();
    const fnc_setting_t* st = fncset_at((size_t)s_sel);
    if (st) {
        fncset_write(st->name, lv_textarea_get_text(s_edit_ta));
        // The reply is the only evidence the write landed — error:N or ok
        // arrives as a console line, so show the console rather than letting
        // a rejected write look identical to a successful one.
        expanded_open(EXPANDED_CONSOLE);
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
    mon_stop();
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
    // The LIMITS button is gone: the lamp row below is the live limit view.
    static const char* CMDS[2]  = { "$CI", "$GD" };
    static const char* LABEL[2] = { "CHANNELS", "GPIO" };
    for (int i = 0; i < 2; i++) {
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
        mon_stop();
        send_line("$CD=config.yaml");
        expanded_open(EXPANDED_CONSOLE);
    }, LV_EVENT_LONG_PRESSED, nullptr);

    // --- live limit/probe lamps ---
    lv_obj_t* pins = lv_obj_create(right);
    w_flatten(pins);
    lv_obj_set_size(pins, LV_PCT(100), U * 5);
    lv_obj_set_flex_flow(pins, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pins, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pins, U / 2, 0);
    lv_obj_t* pt = w_label(pins, "LIMITS", &lv_font_montserrat_10, C_DIM);
    lv_obj_set_width(pt, U * 6);
    for (int i = 0; i < NLAMPS; i++) {
        s_pin_box[i] = w_panel(pins, C_RAISE);
        lv_obj_set_flex_grow(s_pin_box[i], 1);
        lv_obj_set_height(s_pin_box[i], LV_PCT(100));
        s_pin_lbl[i] = w_label(s_pin_box[i], PIN_NAMES[i], &lv_font_montserrat_14, C_DIM);
        lv_obj_center(s_pin_lbl[i]);
        lv_obj_add_flag(s_pin_box[i], LV_OBJ_FLAG_HIDDEN);   // shown per config scan
        s_pin_shown[i] = false;
        s_pin_state[i] = -1;
    }

    s_kb = w_keyboard();
    lv_obj_add_event_cb(s_kb, kb_done_cb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_kb, kb_done_cb, LV_EVENT_CANCEL, nullptr);
}

void page_diag_tick() {
    if (fncset_dirty_take()) {
        refresh();
        scan_probe_config();
    }

    // The lamp's data comes from the $CD-fed caps model now, not from $S, so
    // it must also rescan when caps move on their own — a lone $CD typed into
    // the command box arrives with no fncset dirty tick attached.
    static uint32_t caps_seen = (uint32_t)-1;
    if (caps_seen != caps_generation()) {
        caps_seen = caps_generation();
        scan_probe_config();
    }

    // Run the monitor only while this page is on screen and the machine is
    // not in motion; leaving the page stops it (page_diag_hidden). Alarm is
    // deliberately included: a hard-limit trip is exactly when someone stands
    // here watching these lamps (FluidNC refuses the command only in
    // Cycle/Hold). 10s of silence = dead, refused or unparseable: STOP it
    // (never just forget it — see mon_stop) and retry; two sampleless
    // attempts in a row and we give up until the page is next opened, rather
    // than start/stop the controller forever. The 10s grace must outlast a
    // full $S dump, which can be queued ahead of the monitor request.
    uint32_t now = (uint32_t)milliseconds();
    if (s_alive && (int32_t)(now - s_last) > 10000) {
        if (!s_got_sample) s_dry++;
        mon_stop();
    }
    if (!s_alive && s_dry < 2 && (state == Idle || state == Alarm)) {
        s_alive      = true;
        s_got_sample = false;
        s_last       = now;
        send_line("$Limits/Show");
    }

    for (int i = 0; i < NLAMPS; i++) {
        bool show = (s_cfg_mask >> i) & 1;
        if (show != s_pin_shown[i]) {
            s_pin_shown[i] = show;
            if (show) lv_obj_clear_flag(s_pin_box[i], LV_OBJ_FLAG_HIDDEN);
            else      lv_obj_add_flag(s_pin_box[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (!show) continue;
        int on = (i == 12) ? (myProbeSwitch ? 1 : 0) : ((s_live_mask >> i) & 1);
        if (on == s_pin_state[i]) continue;
        s_pin_state[i] = on;
        lv_obj_set_style_bg_color(s_pin_box[i], on ? C_OK : C_RAISE, 0);
        lv_obj_set_style_text_color(s_pin_lbl[i], on ? C_BG : C_DIM, 0);
    }
}
