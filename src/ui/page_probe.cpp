#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "model/caps.h"
#include "model/settings.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <string.h>

// Plate settings: set once for the plate on the machine, so they live behind
// EDIT and persist, rather than sitting as +/- rows on every visit. Each plate
// type owns its rows; the popup shows only the selected type's.
struct param_t {
    const char* name;
    const char* key;      // NVS key
    float       def, step, lo, hi;
    uint8_t     type;     // 0 flat, 1 AutoZero
    float       value;
    lv_obj_t*   view;     // read-only value on the page (flat rows only)
    lv_obj_t*   edit;     // value in the open popup
};

enum { P_OFFSET, P_TRAVEL, P_FEED, P_RETRACT, P_AZ_FLOOR, P_AZ_CENTRE, P_AZ_LIFT, P_N };

static param_t s_params[P_N] = {
    { "Tool offset",   "prb_offset",  0.0f,   0.1f,  0.0f, 50.0f,   0 },
    { "Max travel",    "prb_travel",  25.0f,  1.0f,  1.0f, 200.0f,  0 },
    { "Feed rate",     "prb_feed",    100.0f, 10.0f, 10.0f, 2000.0f, 0 },
    { "Retract",       "prb_retract", 2.0f,   0.5f,  0.0f, 50.0f,   0 },
    // Sienci AutoZero geometry, defaults from gSender: pocket floor height
    // above the stock (zThickness.autoZero), pocket centre from the stock
    // corner in X and Y (determineAutoPlateOffsetValues), and the final lift
    // above the floor, which must clear the plate's rim.
    { "Pocket floor",  "az_floor",    5.0f,   0.1f,  0.0f, 30.0f,   1 },
    { "Pocket centre", "az_centre",   22.5f,  0.1f,  0.0f, 100.0f,  1 },
    { "Lift after",    "az_lift",     20.0f,  1.0f,  1.0f, 100.0f,  1 },
};

static lv_obj_t* s_popup;
static lv_obj_t* s_az_note;

static lv_obj_t* s_gcode_lbl;
static lv_obj_t* s_go;
static lv_obj_t* s_state_key;
static lv_obj_t* s_state_val;
static lv_obj_t* s_go_lbl;
static lv_obj_t* s_tlo;
static lv_obj_t* s_flat_box;      // flat-plate parameter rows
static lv_obj_t* s_az_box;        // AutoZero corner picker
static lv_obj_t* s_type_btn[2];
static lv_obj_t* s_corner_btn[4];

// ---- Sienci AutoZero Touch Plate ------------------------------------------
// Sequence from gSender's get3AxisAutoRoutine (src/app/src/lib/Probing.ts):
// the bit sits in the plate's pocket, touches the pocket floor for Z, then
// both walls in X and in Y. gSender does the centring maths itself between
// sends; here FluidNC does it, reading each touch from #5061/#5062 (probe
// position, machine coordinates) and driving to the midpoint with G53, so the
// whole routine is one stream of lines and bit diameter never enters into it.
static const char* const CORNER_NAME[4] = { "BOTTOM LEFT", "TOP LEFT", "TOP RIGHT", "BOTTOM RIGHT" };
// Pocket centre relative to the corner, per corner (gSender determineAutoPlateOffsetValues).
static const int8_t CORNER_SX[4] = { 1, 1, -1, -1 };
static const int8_t CORNER_SY[4] = { 1, -1, -1, 1 };

static void az_probe() {
    const int c = settings_probe_corner();
    static const char* const WALLS[] = {
        // Z: pocket floor, fast then slow.
        "G21 G91",
        "G38.2 Z-25 F200",
        "G0 Z2",
        "G38.2 Z-5 F75",
        nullptr,   // G10 Z, filled below
        "G0 Z3",
        // X: left wall, right wall, centre between them.
        "G0 X-13",
        "G38.2 X-30 F150",
        "G0 X2",
        "G38.2 X-5 F75",
        "#<az_a>=#5061",
        "G0 X26",
        "G38.2 X30 F150",
        "G0 X-2",
        "G38.2 X5 F75",
        "G90 G53 G0 X[[#<az_a>+#5061]/2]",
        // Y: same again.
        "G91 G0 Y-13",
        "G38.2 Y-30 F150",
        "G0 Y2",
        "G38.2 Y-5 F75",
        "#<az_a>=#5062",
        "G0 Y26",
        "G38.2 Y30 F150",
        "G0 Y-2",
        "G38.2 Y5 F75",
        "G90 G53 G0 Y[[#<az_a>+#5062]/2]",
    };
    for (const char* l : WALLS) {
        if (l) send_line(l);
        else   send_linef("G10 L20 P0 Z%.3f", (double)s_params[P_AZ_FLOOR].value);
    }
    send_linef("G10 L20 P0 X%.3f Y%.3f",
               (double)(CORNER_SX[c] * s_params[P_AZ_CENTRE].value),
               (double)(CORNER_SY[c] * s_params[P_AZ_CENTRE].value));
    // Out of the pocket before anything moves sideways; X/Y stay put so the
    // plate can be lifted off. gSender's final X0Y0 move is left to GOTO.
    send_linef("G90 G0 Z%.3f", (double)(s_params[P_AZ_FLOOR].value + s_params[P_AZ_LIFT].value));
}

static void show_value(lv_obj_t* lbl, float v) {
    if (!lbl) return;
    char b[24];
    snprintf(b, sizeof(b), "%.2f", (double)v);
    lv_label_set_text(lbl, b);
}

static void refresh_param(int i) {
    show_value(s_params[i].view, s_params[i].value);
    show_value(s_params[i].edit, s_params[i].value);
    if (s_az_note && s_params[i].type == 1) {
        char b[80];
        snprintf(b, sizeof(b), "floor %.2f   centre %.2f   lift %.2f",
                 (double)s_params[P_AZ_FLOOR].value, (double)s_params[P_AZ_CENTRE].value,
                 (double)s_params[P_AZ_LIFT].value);
        lv_label_set_text(s_az_note, b);
    }
}

static void refresh_gcode() {
    // No probe pin: the label is busy explaining WHY the button is grey.
    // Painting G-code over that (a +/- tap lands here) loses the reason for
    // good, since the caps generation never moves again on a probe-less rig.
    if (!cap_ok(caps_probe()) || w_locked()) return;
    char b[64];
    if (settings_probe_type() == 1)
        snprintf(b, sizeof(b), "AutoZero - %s corner", CORNER_NAME[settings_probe_corner()]);
    else
        snprintf(b, sizeof(b), "G38.2 G91 F%d Z-%d",
                 (int)s_params[P_FEED].value, (int)s_params[P_TRAVEL].value);
    lv_label_set_text(s_gcode_lbl, b);
}

static void paint_choice(lv_obj_t* b, bool sel) {
    lv_obj_set_style_bg_color(b, sel ? C_SEL_BG : C_RAISE, 0);
    lv_obj_set_style_border_color(b, sel ? C_ACCENT : C_LINE, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(lv_obj_get_child(b, 0), 0), sel ? C_ACCENT : C_DIM, 0);
}

static void paint_probe_type() {
    const uint8_t t = settings_probe_type();
    for (int i = 0; i < 2; i++) paint_choice(s_type_btn[i], i == t);
    for (int i = 0; i < 4; i++) paint_choice(s_corner_btn[i], i == settings_probe_corner());
    if (t == 1) { lv_obj_add_flag(s_flat_box, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s_az_box, LV_OBJ_FLAG_HIDDEN); }
    else        { lv_obj_add_flag(s_az_box, LV_OBJ_FLAG_HIDDEN);   lv_obj_clear_flag(s_flat_box, LV_OBJ_FLAG_HIDDEN); }
    lv_label_set_text(s_go_lbl, t == 1 ? "PROBE XYZ" : "PROBE Z");
    refresh_gcode();
}

static void type_cb(lv_event_t* e) {
    settings_set_probe_type((uint8_t)(intptr_t)lv_event_get_user_data(e));
    paint_probe_type();
}

static void corner_cb(lv_event_t* e) {
    settings_set_probe_corner((uint8_t)(intptr_t)lv_event_get_user_data(e));
    paint_probe_type();
}

static void bump_cb(lv_event_t* e) {
    int packed = (int)(intptr_t)lv_event_get_user_data(e);
    param_t& p = s_params[packed >> 1];
    p.value += ((packed & 1) ? 1 : -1) * p.step;
    if (p.value < p.lo) p.value = p.lo;
    if (p.value > p.hi) p.value = p.hi;
    refresh_param(packed >> 1);
    refresh_gcode();
}

// One NVS write per value on close, not per +/- tap: every write stalls the
// flash cache and costs the panel a resync.
static void popup_close(lv_event_t*) {
    if (!s_popup) return;
    for (int i = 0; i < P_N; i++) {
        settings_set_float(s_params[i].key, s_params[i].value);
        s_params[i].edit = nullptr;
    }
    lv_obj_del(s_popup);
    s_popup = nullptr;
}

// A settings row: name, then either the value alone (page) or - value + (popup).
static lv_obj_t* param_row(lv_obj_t* parent, int i, bool editable) {
    lv_obj_t* row = w_panel(parent, C_SURFACE);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);   // the page's rows flex-grow past this
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, U + 4, 0);
    lv_obj_set_style_pad_right(row, editable ? 0 : U + 4, 0);

    lv_obj_t* nm = w_label(row, s_params[i].name, &lv_font_montserrat_14, editable ? C_TEXT : C_DIM);
    lv_obj_set_flex_grow(nm, 1);

    lv_obj_t* minus = nullptr;
    if (editable) {
        minus = w_button(row, "-", nullptr, C_RAISE, C_ACCENT, bump_cb, (void*)(intptr_t)((i << 1) | 0));
        lv_obj_set_size(minus, TOUCH_TAP, TOUCH_TAP);
    }
    lv_obj_t* v = w_label(row, "0.00", &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(v, U * 10);
    lv_obj_set_style_text_align(v, editable ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_RIGHT, 0);
    if (editable) {
        lv_obj_t* plus = w_button(row, "+", nullptr, C_RAISE, C_ACCENT, bump_cb, (void*)(intptr_t)((i << 1) | 1));
        lv_obj_set_size(plus, TOUCH_TAP, TOUCH_TAP);
        s_params[i].edit = v;
    } else {
        s_params[i].view = v;
    }
    refresh_param(i);
    return row;
}

// Same scrim-and-card shape as the GOTO popup: top layer, tap off to close.
static void edit_cb(lv_event_t*) {
    if (s_popup) return;
    const uint8_t t = settings_probe_type();

    s_popup = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_popup);
    lv_obj_set_size(s_popup, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_popup, C_BG, 0);
    lv_obj_set_style_bg_opa(s_popup, LV_OPA_80, 0);
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_popup, popup_close, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* card = w_panel(s_popup, C_SURFACE);
    lv_obj_set_size(card, U * 56, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_pad_all(card, U, 0);
    lv_obj_set_style_pad_row(card, U, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, [](lv_event_t* e) { lv_event_stop_bubbling(e); },
                        LV_EVENT_CLICKED, nullptr);

    w_label(card, t == 1 ? "AUTOZERO SETTINGS" : "FLAT PLATE SETTINGS", &lv_font_montserrat_20, C_ACCENT);
    for (int i = 0; i < P_N; i++)
        if (s_params[i].type == t) param_row(card, i, true);

    lv_obj_t* done = w_button(card, "DONE", "saved", C_ACCENT, C_BG, popup_close, nullptr);
    lv_obj_set_size(done, LV_PCT(100), TOUCH_TAP);
}

// Sequence copied from FluidDial-main/src/ProbingScene.cpp:29 and :51 rather
// than re-derived: probe down, then retract by the configured distance.
static void probe_cb(lv_event_t*) {
    if (settings_probe_type() == 1) { az_probe(); return; }
    send_linef("G38.2G91F%dZ-%dP%.2f",
               (int)s_params[P_FEED].value, (int)s_params[P_TRAVEL].value, (double)s_params[P_OFFSET].value);
    send_linef("$J=G91F1000Z%.2f", (double)s_params[P_RETRACT].value);
}

static void tlo_cb(lv_event_t*) {
    const int m = caps_tlo_macro();
    if (m >= 0 && caps_macro_num(m) >= 0) send_linef("$RM=%d", caps_macro_num(m));
}

void page_probe_build(lv_obj_t* p) {
    for (int i = 0; i < P_N; i++)
        s_params[i].value = settings_get_float(s_params[i].key, s_params[i].def);

    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p, U, 0);

    lv_obj_t* left = lv_obj_create(p);
    w_flatten(left);
    lv_obj_set_size(left, U * 55, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, U, 0);

    // Plate type: which of the two boxes below is live.
    lv_obj_t* types = lv_obj_create(left);
    w_flatten(types);
    lv_obj_set_size(types, LV_PCT(100), U * 6);
    lv_obj_set_flex_flow(types, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(types, U, 0);
    static const char* const TYPE_NAME[2] = { "FLAT PLATE", "SIENCI AUTOZERO" };
    for (int i = 0; i < 2; i++) {
        s_type_btn[i] = w_button(types, TYPE_NAME[i], nullptr, C_RAISE, C_DIM, type_cb, (void*)(intptr_t)i);
        lv_obj_set_flex_grow(s_type_btn[i], 1);
        lv_obj_set_height(s_type_btn[i], LV_PCT(100));
    }
    lv_obj_t* edit = w_button(types, "EDIT", nullptr, C_RAISE, C_ACCENT, edit_cb, nullptr);
    lv_obj_set_size(edit, U * 10, LV_PCT(100));

    s_flat_box = lv_obj_create(left);
    w_flatten(s_flat_box);
    lv_obj_set_width(s_flat_box, LV_PCT(100));
    lv_obj_set_flex_grow(s_flat_box, 1);
    lv_obj_set_flex_flow(s_flat_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_flat_box, U, 0);

    // AutoZero: pick the stock corner the plate sits on, laid out as the
    // corners are, so the tap is where the plate is.
    s_az_box = lv_obj_create(left);
    w_flatten(s_az_box);
    lv_obj_set_width(s_az_box, LV_PCT(100));
    lv_obj_set_flex_grow(s_az_box, 1);
    static lv_coord_t acs[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t ars[] = { LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(s_az_box, acs, ars);
    lv_obj_set_layout(s_az_box, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(s_az_box, U, 0);
    lv_obj_set_style_pad_row(s_az_box, U, 0);
    lv_obj_t* hint = w_label(s_az_box, "STOCK CORNER - bit in the pocket, below its rim",
                             &lv_font_montserrat_10, C_DIM);
    lv_obj_set_grid_cell(hint, LV_GRID_ALIGN_START, 0, 2, LV_GRID_ALIGN_CENTER, 0, 1);
    s_az_note = w_label(s_az_box, "", &lv_font_montserrat_10, C_DIM);
    lv_obj_set_grid_cell(s_az_note, LV_GRID_ALIGN_START, 0, 2, LV_GRID_ALIGN_CENTER, 3, 1);
    // Grid position of each gSender corner: BL, TL, TR, BR.
    static const uint8_t COL[4] = { 0, 0, 1, 1 }, ROW[4] = { 2, 1, 1, 2 };
    for (int i = 0; i < 4; i++) {
        s_corner_btn[i] = w_button(s_az_box, CORNER_NAME[i], nullptr, C_RAISE, C_DIM, corner_cb, (void*)(intptr_t)i);
        lv_obj_set_grid_cell(s_corner_btn[i], LV_GRID_ALIGN_STRETCH, COL[i], 1, LV_GRID_ALIGN_STRETCH, ROW[i], 1);
    }

    for (int i = 0; i < P_N; i++) {
        if (s_params[i].type != 0) continue;
        lv_obj_set_flex_grow(param_row(s_flat_box, i, false), 1);
    }
    refresh_param(P_AZ_FLOOR);   // fills the AutoZero summary line

    lv_obj_t* right = lv_obj_create(p);
    w_flatten(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, U, 0);

    // A readout, not a control: one strip, so the column has room for TLO.
    lv_obj_t* st = w_panel(right, C_SURFACE);
    lv_obj_set_size(st, LV_PCT(100), TOUCH_TAP);
    s_state_key = w_label(st, "PROBE INPUT", &lv_font_montserrat_10, C_DIM);
    lv_obj_align(s_state_key, LV_ALIGN_LEFT_MID, U, 0);
    s_state_val = w_label(st, "OPEN", &lv_font_montserrat_20, C_OK);
    lv_obj_align(s_state_val, LV_ALIGN_RIGHT_MID, -U, 0);

    // Measures the tool in the spindle on the fixed toolsetter and applies
    // the length offset: the controller's own TLO macro, found from the config.
    s_tlo = w_button(right, "TLO FROM SENSOR", "$RM", C_ACCENT, C_BG, tlo_cb, nullptr);
    lv_obj_set_width(s_tlo, LV_PCT(100));
    lv_obj_set_flex_grow(s_tlo, 1);

    s_go = lv_btn_create(right);
    lv_obj_set_width(s_go, LV_PCT(100));
    lv_obj_set_flex_grow(s_go, 1);
    lv_obj_set_style_bg_color(s_go, C_WARN, 0);
    lv_obj_set_style_radius(s_go, 3, 0);
    lv_obj_set_style_shadow_width(s_go, 0, 0);
    lv_obj_set_style_opa(s_go, OPA_DISABLED, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_go, probe_cb, LV_EVENT_CLICKED, nullptr);
    s_go_lbl = w_label(s_go, "PROBE Z", &lv_font_montserrat_20, C_BG);
    lv_obj_align(s_go_lbl, LV_ALIGN_CENTER, 0, -10);
    s_gcode_lbl = w_label(s_go, "", &lv_font_montserrat_12, C_BG);
    lv_obj_align(s_gcode_lbl, LV_ALIGN_CENTER, 0, 14);
    paint_probe_type();

    page_probe_tick();
}

// G38.2 against a machine with /probe/pin NO_PIN runs the full travel and then
// alarms, having moved the tool the whole way down. Refusing to send it is the
// difference between a greyed button and a crashed cutter.
void page_probe_tick() {
    const bool ok = cap_ok(caps_probe());

    // Re-gate when the caps OR the alarm lock change. No probe pin outranks
    // the alarm as the reason: it is the one unlocking will not fix.
    const bool locked = w_locked();
    static uint32_t seen = (uint32_t)-1;
    static int seen_lock = -1;
    if (seen != caps_generation() || seen_lock != locked) {
        seen = caps_generation();
        seen_lock = locked;
        if (ok && !locked) {
            lv_obj_clear_state(s_go, LV_STATE_DISABLED);
            lv_obj_add_flag(s_go, LV_OBJ_FLAG_CLICKABLE);
            refresh_gcode();
        } else {
            lv_obj_add_state(s_go, LV_STATE_DISABLED);
            lv_obj_clear_flag(s_go, LV_OBJ_FLAG_CLICKABLE);
            lv_label_set_text(s_gcode_lbl, ok ? LOCK_REASON : "probe: pin is NO_PIN");
        }
        lv_label_set_text(s_state_key, ok ? "PROBE INPUT" : "NO PROBE PIN");
        lv_obj_set_style_text_color(s_state_val, ok ? C_OK : C_DIM, 0);
    }

    // TLO: needs the macro, and a machine sitting still to run it on.
    {
        const int m = caps_tlo_macro();
        const bool idle = state == Idle;
        static int seen_m = -2, seen_idle = -1, seen_l = -1;
        if (m != seen_m || idle != seen_idle || locked != seen_l) {
            seen_m = m; seen_idle = idle; seen_l = locked;
            char sub[40];
            snprintf(sub, sizeof(sub), "$RM=%d", m >= 0 ? caps_macro_num(m) : -1);
            w_set_sub(s_tlo, sub);
            w_gate(s_tlo, m >= 0 && idle,
                   m < 0 ? "no TLO macro in config" : locked ? LOCK_REASON : "machine busy");
        }
    }

    // Live, every tick, NOT gated on caps: the whole point of this readout is
    // touching the probe to the tool and watching it flip before trusting
    // G38.2, and caps go quiet long before anyone picks the probe up.
    const char* txt = ok ? (myProbeSwitch ? "CLOSED" : "OPEN") : "--";
    if (strcmp(lv_label_get_text(s_state_val), txt) != 0)
        lv_label_set_text(s_state_val, txt);
}
