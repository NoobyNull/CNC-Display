#include "ui/goto_popup.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "model/fnc_settings.h"
#include "model/offsets.h"
#include "GrblParserC.h"
#include "e4math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Tap an axis in the DRO, type a coordinate, go there.
//
// The step selector tops out at 10mm, so crossing 400mm meant forty presses —
// or holding a key at a repeat rate that is deliberately slow because
// hold-to-jog is capped at 1mm steps. Neither is a way to traverse a table.
//
// The move is sent as `$J=G90F<rate><axis><target>`: a JOG, not a G0.
//   - Jog does not touch the g-code modal state. A bare `G0 X400` would be
//     interpreted against whatever G90/G91 mode the machine is in, and in G91
//     that is a 400mm RELATIVE move — the same keystroke meaning two wildly
//     different things is not acceptable for a control that moves a gantry.
//   - Jog is interruptible: feed hold stops it, and the safety bar is on every
//     screen including this one.
//   - G90 inside the jog line makes the target absolute in the active work
//     coordinate system, which is the number the operator is reading off the
//     DRO.

static lv_obj_t* s_root;
static lv_obj_t* s_entry;
static lv_obj_t* s_note;
static lv_obj_t* s_go;
static int       s_axis;
static char      s_buf[16];
static lv_obj_t* s_mode_abs;
static lv_obj_t* s_mode_rel;

// Which positioning the typed number means. Never inferred, never inherited
// from the machine's modal state: the operator picks it here and the popup says
// so in three places at once — the selected button, the note under the entry,
// and the g-code on the GO key. "400" meaning either "go to 400" or "move 400
// further" with no visible difference is how a gantry meets its end stop.
static bool s_absolute = true;

// When set, the card is a plain number pad: no axis, no modes, no travel check,
// and GO hands the value to this instead of jogging.
static void (*s_numcb)(float) = nullptr;
static char s_num_title[24];
static char s_num_note[48];

// Feed comes from the axis's own configured maximum, not a constant: a rotary
// and a Z ballscrew do not traverse at the same rate, and the controller
// already knows both. Only if the settings dump has not arrived does this fall
// back, and it falls back slow.
static const float GOTO_FALLBACK_FEED = 1000.0f;

static float axis_setting(const char* prefix, int axis, float fallback) {
    char key[FS_NAME_MAX];
    snprintf(key, sizeof(key), "%s%c", prefix, axisNumToChar(axis));
    for (size_t i = 0; i < fncset_count(); i++) {
        const fnc_setting_t* s = fncset_at(i);
        if (s && strcmp(s->name, key) == 0) {
            float v = (float)atof(s->value);
            if (v > 0.0f) return v;
        }
    }
    return fallback;
}

static void refresh();

// Repaint BOTH states. w_button() installs a pressed-state background computed
// from the colour the button had when it was built, and that is the style the
// button is still wearing at the moment its click handler runs — so repainting
// only the default state left the highlight looking one tap behind.
static void mode_btn_paint(lv_obj_t* b, bool on, lv_color_t on_col) {
    const lv_color_t bg = on ? on_col : C_RAISE;
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_color(b, lv_color_lighten(bg, 40), LV_STATE_PRESSED);
    // Contrast has to follow the fill, or the caption vanishes into the accent.
    lv_obj_t* col = lv_obj_get_child(b, 0);
    if (col) {
        lv_obj_t* cap = lv_obj_get_child(col, 0);
        lv_obj_t* sub = lv_obj_get_child(col, 1);
        if (cap) lv_obj_set_style_text_color(cap, on ? C_BG : C_TEXT, 0);
        if (sub) lv_obj_set_style_text_color(sub, on ? C_BG : C_DIM, 0);
    }
}

static void mode_paint() {
    mode_btn_paint(s_mode_abs, s_absolute,  C_ACCENT);
    mode_btn_paint(s_mode_rel, !s_absolute, C_WARN);
    // The GO key states the g-code it will actually send, and must follow the
    // toggle rather than whatever the mode was when the popup was built.
    if (s_go) {
        lv_obj_t* col = lv_obj_get_child(s_go, 0);
        lv_obj_t* sub = col ? lv_obj_get_child(col, 1) : nullptr;
        if (sub) lv_label_set_text(sub, s_absolute ? "G90 abs" : "G91 rel");
    }
}

static void refresh() {
    if (s_numcb) {
        lv_label_set_text(s_entry, s_buf[0] ? s_buf : "0");
        lv_label_set_text(s_note, s_num_note);
        lv_obj_set_style_text_color(s_note, C_DIM, 0);
        if (s_buf[0]) lv_obj_clear_state(s_go, LV_STATE_DISABLED);
        else          lv_obj_add_state(s_go, LV_STATE_DISABLED);
        return;
    }
    mode_paint();
    // The entry itself carries the sign convention: a relative move reads as
    // an explicit delta, so "+400" and "400" cannot be confused at a glance.
    char shown[24];
    if (s_absolute) snprintf(shown, sizeof(shown), "%s", s_buf[0] ? s_buf : "0");
    else            snprintf(shown, sizeof(shown), "%s%s",
                             (s_buf[0] && s_buf[0] != '-') ? "+" : "", s_buf[0] ? s_buf : "0");
    lv_label_set_text(s_entry, shown);

    const bool idle  = (state == Idle);
    const bool typed = s_buf[0] && strcmp(s_buf, "-") != 0;

    char note[96];
    if (!idle) {
        // Refusing while the machine is busy is the whole point: a goto issued
        // mid-cut is a crash, and jog is rejected in Cycle anyway.
        snprintf(note, sizeof(note), "machine must be IDLE to go to a position");
        lv_obj_set_style_text_color(s_note, C_WARN, 0);
    } else if (!typed) {
        if (s_absolute) snprintf(note, sizeof(note), "ABSOLUTE - move TO this %s coordinate", offsets_active());
        else            snprintf(note, sizeof(note), "RELATIVE - move BY this much from here");
        lv_obj_set_style_text_color(s_note, C_DIM, 0);
    } else {
        // Reports the coordinates the move resolves to, and stops there.
        //
        // This used to compare the result against $Grbl/MaxTravel and colour a
        // warning. That went: the comparison is only meaningful once the machine
        // has homed, because until then machine coordinates have an arbitrary
        // origin — so it could warn when nothing was wrong and stay silent when
        // something was. And travel limits are the controller's business. The
        // panel states where the move goes; deciding whether that is allowed
        // has exactly one owner.
        // In relative mode the typed value is a delta, so the destination is
        // current work position plus it. Both modes then resolve to the same
        // machine coordinate, which is what is reported.
        const float here   = (float)atof(e4_to_cstr(myAxes[s_axis], 4));
        const float target = s_absolute ? (float)atof(s_buf) : here + (float)atof(s_buf);
        const float mach   = target + (float)atof(e4_to_cstr(myWco[s_axis], 4));
        snprintf(note, sizeof(note), "%s -> work %.2f / machine %.2f",
                 s_absolute ? "ABS" : "REL", (double)target, (double)mach);
        lv_obj_set_style_text_color(s_note, C_DIM, 0);
    }
    lv_label_set_text(s_note, note);

    if (idle && typed) lv_obj_clear_state(s_go, LV_STATE_DISABLED);
    else               lv_obj_add_state(s_go, LV_STATE_DISABLED);
}

static void close_cb(lv_event_t*) { goto_close(); }

static void key_cb(lv_event_t* e) {
    const char* k = (const char*)lv_event_get_user_data(e);
    size_t n = strlen(s_buf);

    if (!strcmp(k, "C")) {
        s_buf[0] = 0;
    } else if (!strcmp(k, "<")) {
        if (n) s_buf[n - 1] = 0;
    } else if (!strcmp(k, "-")) {
        // Sign toggles rather than inserts: a minus anywhere but the front is
        // not a number, and typing it twice should undo.
        if (s_buf[0] == '-') memmove(s_buf, s_buf + 1, n);
        else if (n + 1 < sizeof(s_buf)) { memmove(s_buf + 1, s_buf, n + 1); s_buf[0] = '-'; }
    } else if (!strcmp(k, ".")) {
        if (!strchr(s_buf, '.') && n + 1 < sizeof(s_buf)) { s_buf[n] = '.'; s_buf[n + 1] = 0; }
    } else if (n + 1 < sizeof(s_buf)) {
        s_buf[n] = k[0];
        s_buf[n + 1] = 0;
    }
    refresh();
}

static void go_cb(lv_event_t*) {
    if (s_numcb) {
        if (!s_buf[0]) return;
        void (*cb)(float) = s_numcb;
        const float v = (float)atof(s_buf);
        goto_close();
        cb(v);            // after close: the callback may open something else
        return;
    }
    if (state != Idle || !s_buf[0]) return;
    const float feed = axis_setting("Grbl/MaxRate/", s_axis, GOTO_FALLBACK_FEED);
    char line[64];
    // G90 absolute / G91 incremental, stated inside the jog line so it cannot
    // be affected by whatever modal state the controller happens to hold.
    snprintf(line, sizeof(line), "$J=%sF%.3f%c%.4f",
             s_absolute ? "G90" : "G91",
             (double)feed, axisNumToChar(s_axis), (double)atof(s_buf));
    send_jog_line(line);
    goto_close();
}

bool goto_is_open() { return s_root != nullptr; }

void goto_close() {
    if (!s_root) return;
    s_numcb = nullptr;
    lv_obj_del(s_root);
    s_root = nullptr;
}

// Opens the same card as a plain number pad. The axis/mode row is skipped and
// GO reports the value instead of moving anything.
void numpad_open(const char* title, const char* note, float initial,
                 void (*cb)(float)) {
    if (s_root || !cb) return;
    s_numcb = cb;
    snprintf(s_num_title, sizeof(s_num_title), "%s", title ? title : "VALUE");
    snprintf(s_num_note,  sizeof(s_num_note),  "%s", note  ? note  : "");
    if (initial > 0.0f) snprintf(s_buf, sizeof(s_buf), "%g", (double)initial);
    else                s_buf[0] = 0;
    goto_open(-2);          // -2: build the card, skip the axis machinery
}

void goto_open(int axis) {
    // -2 is the numpad path, which has no axis to validate.
    if (axis != -2 && (s_root || axis < 0 || axis >= n_axes)) return;
    if (axis == -2 && s_root) return;
    if (axis != -2) {
        s_axis = axis;
        s_buf[0] = 0;
        s_absolute = true;   // every open starts in the safer of the two
    }

    // Full-screen scrim on the top layer: the popup must sit above the rail and
    // the DRO, and tapping off it is the same as cancelling.
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_root, C_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_80, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_root, close_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* card = w_panel(s_root, C_SURFACE);
    lv_obj_set_size(card, U * 56, U * 50);
    lv_obj_center(card);
    lv_obj_set_style_pad_all(card, U, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, U / 2, 0);
    // Swallow taps on the card so they do not reach the scrim behind it.
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, [](lv_event_t* e) { lv_event_stop_bubbling(e); },
                        LV_EVENT_CLICKED, nullptr);

    char title[24];
    if (s_numcb) snprintf(title, sizeof(title), "%s", s_num_title);
    else         snprintf(title, sizeof(title), "GO TO  %c", axisNumToChar(axis));
    w_label(card, title, &lv_font_montserrat_20, C_ACCENT);

    lv_obj_t* modes = lv_obj_create(card);
    if (s_numcb) lv_obj_add_flag(modes, LV_OBJ_FLAG_HIDDEN);
    w_flatten(modes);
    lv_obj_set_size(modes, LV_PCT(100), TOUCH_TAP);
    lv_obj_set_flex_flow(modes, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(modes, U / 2, 0);
    s_mode_abs = w_button(modes, "ABSOLUTE", "G90 - move to", C_RAISE, C_TEXT,
                          [](lv_event_t*) { s_absolute = true;  refresh(); }, nullptr);
    s_mode_rel = w_button(modes, "RELATIVE", "G91 - move by", C_RAISE, C_TEXT,
                          [](lv_event_t*) { s_absolute = false; refresh(); }, nullptr);
    lv_obj_set_flex_grow(s_mode_abs, 1); lv_obj_set_height(s_mode_abs, LV_PCT(100));
    lv_obj_set_flex_grow(s_mode_rel, 1); lv_obj_set_height(s_mode_rel, LV_PCT(100));

    // Per Matthew: tapping into an axis is also where you revive its driver.
    // Only on machines whose driver wiring reports (feature armed). All five
    // drivers have reset relays (A's landed 2026-09-07, slot P4).
    const char axc = (axis >= 0) ? axisNumToChar(axis) : 0;
    if (!s_numcb && drv_feature_armed()
        && (axc == 'X' || axc == 'Y' || axc == 'Z' || axc == 'A')) {
        lv_obj_t* rst = w_button(modes, "RESET DRIVE",
                                 axc == 'Y' ? "cycle Y1+Y2" : "power-cycle",
                                 C_RAISE, C_WARN,
                                 [](lv_event_t*) {
                                     drv_reset_axis(axisNumToChar(s_axis));
                                     goto_close();
                                 }, nullptr);
        lv_obj_set_flex_grow(rst, 1);
        lv_obj_set_height(rst, LV_PCT(100));
    }

    s_entry = w_label(card, "0", &lv_font_montserrat_28, C_TEXT);
    s_note  = w_label(card, "", &lv_font_montserrat_12, C_DIM);

    // Keypad. A numeric pad rather than the shared text keyboard: that one is a
    // singleton whose handlers already fight between pages (see docs/HANDOFF).
    static const char* KEYS[16] = { "7","8","9","C",
                                    "4","5","6","<",
                                    "1","2","3","-",
                                    "0",".","GO","X" };
    lv_obj_t* pad = lv_obj_create(card);
    w_flatten(pad);
    lv_obj_set_width(pad, LV_PCT(100));
    lv_obj_set_flex_grow(pad, 1);
    lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(pad, U / 2, 0);
    lv_obj_set_style_pad_row(pad, U / 2, 0);

    const lv_coord_t kw = (U * 54 - U * 2) / 4;
    for (int i = 0; i < 16; i++) {
        const char* k = KEYS[i];
        const bool  is_go     = !strcmp(k, "GO");
        const bool  is_cancel = !strcmp(k, "X");
        lv_obj_t* b = w_button(pad,
                               is_cancel ? "CANCEL" : k,
                               is_go ? (s_numcb ? "set" : (s_absolute ? "G90 abs" : "G91 rel")) : nullptr,
                               is_go ? C_OK : is_cancel ? C_RAISE : C_RAISE,
                               is_go ? C_BG : is_cancel ? C_WARN : C_TEXT,
                               is_go ? go_cb : is_cancel ? close_cb : key_cb,
                               (void*)k);
        lv_obj_set_size(b, kw, TOUCH_TAP);
        if (is_go) s_go = b;
    }

    refresh();
}
