#include "ui/shell.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/view_expanded.h"
#include "model/FluidNCModel.h"
#include "model/model_events.h"
#include "model/model_compat.h"
#include "model/offsets.h"
#include "GrblParserC.h"
#include "e4math.h"
#include <stdio.h>

// Eight destinations, a five-item window. 5U + 5*10U + 5U = 60U, exactly the
// panel height, with every target at the full 10U. Fitting all eight at once
// would mean 7.5U items and a broken grid; a nested menu would cost a tap on
// things used constantly.
#define NAV_COUNT   8
#define NAV_VISIBLE 5

static const char* NAV_LABEL[NAV_COUNT] = {
    "JOG", "HOME", "PROBE", "OFFSET", "RUN", "MACRO", "DIAG", "SETUP"
};

static lv_obj_t* s_scr;
static lv_obj_t* s_rail;
static lv_obj_t* s_rail_up;
static lv_obj_t* s_rail_down;
static lv_obj_t* s_nav_btn[NAV_VISIBLE];
static lv_obj_t* s_nav_lbl[NAV_VISIBLE];
static lv_obj_t* s_dro;
static lv_obj_t* s_right;
static lv_obj_t* s_content;
static lv_obj_t* s_safety;
static lv_obj_t* s_pages[PAGE_COUNT];

static lv_obj_t* s_state_chip;
static lv_obj_t* s_state_lbl;
static lv_obj_t* s_wcs_lbl;
static lv_obj_t* s_axis_work[3];
static lv_obj_t* s_axis_mach[3];
static lv_obj_t* s_flip_btn;
static lv_obj_t* s_close_btn;
static lv_obj_t* s_ovr_box;

static int       s_nav_start = 0;
static page_id_t s_page = PAGE_JOG;
static bool      s_expanded = false;

page_id_t shell_current_page() { return s_page; }
bool      shell_expanded()     { return s_expanded; }

// ---- state chip: colour AND word, never colour alone -----------------------
static lv_color_t state_colour(state_t s) {
    switch (s) {
        case Idle:                            return C_OK;
        case Cycle: case Homing:              return C_RUN;
        case Jog:                             return C_ACCENT;
        case Hold: case DoorOpen:
        case DoorClosed:                      return C_WARN;
        case Alarm: case Critical:
        case ConfigAlarm:                     return C_CRIT;
        case Disconnected:                    return C_LINE;
        default:                              return C_DIM;
    }
}

static const char* state_word(state_t s) {
    switch (s) {
        case Idle:         return "IDLE";
        case Cycle:        return "RUN";
        case Jog:          return "JOG";
        case Homing:       return "HOME";
        case Hold:         return "HOLD";
        case DoorOpen:
        case DoorClosed:   return "DOOR";
        case Alarm:        return "ALARM";
        case Critical:     return "CRIT";
        case ConfigAlarm:  return "CONFIG";
        case GrblSleep:    return "SLEEP";
        case CheckMode:    return "CHECK";
        case Disconnected: return "NO LINK";
        default:           return "?";
    }
}

// ---- nav -------------------------------------------------------------------
static void nav_refresh() {
    for (int i = 0; i < NAV_VISIBLE; i++) {
        int idx = s_nav_start + i;
        lv_label_set_text(s_nav_lbl[i], NAV_LABEL[idx]);
        bool on = (idx == (int)s_page);
        lv_obj_set_style_bg_color(s_nav_btn[i], on ? C_ACCENT : C_SURFACE, 0);
        lv_obj_set_style_text_color(s_nav_lbl[i], on ? C_BG : C_DIM, 0);
    }
    // Grey out at each end so the rail never lies about whether there is more.
    lv_obj_set_style_text_color(s_rail_up,
        s_nav_start > 0 ? C_DIM : C_LINE, 0);
    lv_obj_set_style_text_color(s_rail_down,
        s_nav_start < NAV_COUNT - NAV_VISIBLE ? C_DIM : C_LINE, 0);
}

static void nav_scroll_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int n = s_nav_start + dir;
    if (n < 0) n = 0;
    if (n > NAV_COUNT - NAV_VISIBLE) n = NAV_COUNT - NAV_VISIBLE;
    s_nav_start = n;
    nav_refresh();
}

static void nav_pick_cb(lv_event_t* e) {
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    shell_show_page((page_id_t)(s_nav_start + slot));
}

void shell_show_page(page_id_t id) {
    if (id < 0 || id >= PAGE_COUNT) return;
    s_page = id;
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (i == (int)id) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else              lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    // Keep the active item on screen when navigated to programmatically.
    if ((int)id < s_nav_start)                   s_nav_start = (int)id;
    if ((int)id > s_nav_start + NAV_VISIBLE - 1) s_nav_start = (int)id - NAV_VISIBLE + 1;
    nav_refresh();
}

// ---- safety bar: on every screen, always the same place ---------------------
static void hold_cb(lv_event_t*)   { fnc_realtime(FeedHold); }
static void resume_cb(lv_event_t*) { fnc_realtime(CycleStart); }
static void reset_cb(lv_event_t*)  { fnc_realtime(Reset); }
static void flip_cb(lv_event_t*)   { expanded_flip(); }
static void close_cb(lv_event_t*)  { expanded_close(); }

static void build_safety(lv_obj_t* parent) {
    s_safety = lv_obj_create(parent);
    w_flatten(s_safety);
    lv_obj_set_style_bg_color(s_safety, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(s_safety, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s_safety, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(s_safety, C_LINE, 0);
    lv_obj_set_style_border_width(s_safety, 1, 0);
    lv_obj_set_style_pad_all(s_safety, U, 0);
    lv_obj_set_style_pad_column(s_safety, U, 0);
    lv_obj_set_flex_flow(s_safety, LV_FLEX_FLOW_ROW);

    struct { const char* t; lv_color_t bg; lv_event_cb_t cb; } B[3] = {
        { "HOLD",   C_WARN, hold_cb   },
        { "RESUME", C_OK,   resume_cb },
        { "RESET",  C_CRIT, reset_cb  },
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* o = w_button(s_safety, B[i].t, nullptr, B[i].bg, C_BG, B[i].cb, nullptr);
        lv_obj_set_size(o, U * 19, LV_PCT(100));   // 152 x 64
    }

    // Overrides fill the remainder in normal mode; FLIP/CLOSE replace them when
    // the expanded view is open.
    s_ovr_box = lv_obj_create(s_safety);
    w_flatten(s_ovr_box);
    lv_obj_set_flex_grow(s_ovr_box, 1);
    lv_obj_set_height(s_ovr_box, LV_PCT(100));
    lv_obj_set_flex_flow(s_ovr_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_ovr_box, U, 0);

    s_flip_btn = w_button(s_safety, "FLIP", "view", C_ACCENT, C_BG, flip_cb, nullptr);
    lv_obj_set_size(s_flip_btn, U * 11, LV_PCT(100));
    lv_obj_add_flag(s_flip_btn, LV_OBJ_FLAG_HIDDEN);

    s_close_btn = w_button(s_safety, "CLOSE", "back", C_RAISE, C_TEXT, close_cb, nullptr);
    lv_obj_set_size(s_close_btn, U * 11, LV_PCT(100));
    lv_obj_add_flag(s_close_btn, LV_OBJ_FLAG_HIDDEN);
}

// ---- DRO -------------------------------------------------------------------
static void build_dro(lv_obj_t* parent) {
    s_dro = lv_obj_create(parent);
    w_flatten(s_dro);
    lv_obj_set_style_border_side(s_dro, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(s_dro, C_LINE, 0);
    lv_obj_set_style_border_width(s_dro, 1, 0);
    lv_obj_set_flex_flow(s_dro, LV_FLEX_FLOW_ROW);

    lv_obj_t* sc = lv_obj_create(s_dro);
    w_flatten(sc);
    lv_obj_set_size(sc, U * 16, LV_PCT(100));
    lv_obj_set_style_pad_all(sc, U, 0);
    lv_obj_set_style_pad_row(sc, 5, 0);
    lv_obj_set_style_border_side(sc, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(sc, C_LINE, 0);
    lv_obj_set_style_border_width(sc, 1, 0);
    lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sc, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_state_chip = w_panel(sc, C_LINE);
    lv_obj_set_size(s_state_chip, LV_PCT(100), U * 5);
    lv_obj_set_style_border_width(s_state_chip, 0, 0);
    s_state_lbl = w_label(s_state_chip, "NO LINK", &lv_font_montserrat_14, C_DIM);
    lv_obj_center(s_state_lbl);

    s_wcs_lbl = w_label(sc, "G54", &lv_font_montserrat_12, C_ACCENT);
    w_label(sc, "UART 1M", &lv_font_montserrat_10, C_DIM);

    lv_obj_t* axes = lv_obj_create(s_dro);
    w_flatten(axes);
    lv_obj_set_size(axes, CONTENT_W - U * 16, LV_PCT(100));
    lv_obj_set_flex_flow(axes, LV_FLEX_FLOW_ROW);

    static const char* AX[3] = { "X", "Y", "Z" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* c = lv_obj_create(axes);
        w_flatten(c);
        lv_obj_set_size(c, (CONTENT_W - U * 16) / 3, LV_PCT(100));
        lv_obj_set_style_pad_left(c, U * 2, 0);
        lv_obj_set_style_border_side(c, LV_BORDER_SIDE_RIGHT, 0);
        lv_obj_set_style_border_color(c, C_LINE, 0);
        lv_obj_set_style_border_width(c, i < 2 ? 1 : 0, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        w_label(c, AX[i], &lv_font_montserrat_12, C_DIM);
        s_axis_work[i] = w_label(c, "0.000", &lv_font_montserrat_40, C_TEXT);
        s_axis_mach[i] = w_label(c, "M 0.000", &lv_font_montserrat_12, C_DIM);
    }
}

// ---- rail ------------------------------------------------------------------
static void build_rail(lv_obj_t* parent) {
    s_rail = lv_obj_create(parent);
    w_flatten(s_rail);
    lv_obj_set_style_bg_color(s_rail, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(s_rail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s_rail, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(s_rail, C_LINE, 0);
    lv_obj_set_style_border_width(s_rail, 1, 0);
    lv_obj_set_flex_flow(s_rail, LV_FLEX_FLOW_COLUMN);

    s_rail_up = w_button(s_rail, LV_SYMBOL_UP, nullptr, C_RAISE, C_DIM,
                         nav_scroll_cb, (void*)(intptr_t)-1);
    lv_obj_set_size(s_rail_up, LV_PCT(100), RAIL_SCROLL_H);

    for (int i = 0; i < NAV_VISIBLE; i++) {
        lv_obj_t* b = lv_btn_create(s_rail);
        lv_obj_set_size(b, LV_PCT(100), TOUCH_HOLD);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, C_SURFACE, 0);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(b, C_LINE, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_add_event_cb(b, nav_pick_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        s_nav_btn[i] = b;
        s_nav_lbl[i] = w_label(b, NAV_LABEL[i], &lv_font_montserrat_12, C_DIM);
        lv_obj_center(s_nav_lbl[i]);
    }

    s_rail_down = w_button(s_rail, LV_SYMBOL_DOWN, nullptr, C_RAISE, C_DIM,
                           nav_scroll_cb, (void*)(intptr_t)1);
    lv_obj_set_size(s_rail_down, LV_PCT(100), RAIL_SCROLL_H);
}

void shell_build() {
    s_scr = lv_scr_act();
    lv_obj_set_style_bg_color(s_scr, C_BG, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_scr, 0, 0);

    build_rail(s_scr);
    lv_obj_set_pos(s_rail, 0, 0);
    lv_obj_set_size(s_rail, RAIL_W, SCREEN_H);

    s_right = lv_obj_create(s_scr);
    w_flatten(s_right);
    lv_obj_set_pos(s_right, RAIL_W, 0);
    lv_obj_set_size(s_right, CONTENT_W, SCREEN_H);
    lv_obj_set_flex_flow(s_right, LV_FLEX_FLOW_COLUMN);

    build_dro(s_right);
    lv_obj_set_size(s_dro, CONTENT_W, DRO_H);

    s_content = lv_obj_create(s_right);
    w_flatten(s_content);
    lv_obj_set_size(s_content, CONTENT_W, CONTENT_H);
    lv_obj_set_style_pad_all(s_content, U, 0);

    build_safety(s_right);
    lv_obj_set_size(s_safety, CONTENT_W, SAFETY_H);

    page_build_fn BUILD[PAGE_COUNT] = {
        page_jog_build, page_home_build, page_probe_build, page_offsets_build,
        page_run_build, page_macro_build, page_diag_build, page_setup_build
    };
    for (int i = 0; i < PAGE_COUNT; i++) {
        s_pages[i] = lv_obj_create(s_content);
        w_flatten(s_pages[i]);
        lv_obj_set_size(s_pages[i], CONTENT_W - U * 2, CONTENT_H - U * 2);
        lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        BUILD[i](s_pages[i]);
    }

    expanded_build();
    shell_show_page(PAGE_JOG);
    nav_refresh();
}

void shell_set_expanded(bool on) {
    s_expanded = on;
    if (on) {
        lv_obj_add_flag(s_rail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_dro, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_content, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_flip_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_close_btn, LV_OBJ_FLAG_HIDDEN);
        // The safety bar is never covered: 90% of 480 would leave 48px for the
        // controls you hit in a hurry. They keep their full 80px.
        lv_obj_set_parent(s_safety, s_scr);
        lv_obj_set_pos(s_safety, 0, SCREEN_H - SAFETY_H);
        lv_obj_set_size(s_safety, SCREEN_W, SAFETY_H);
        lv_obj_move_foreground(s_safety);
    } else {
        lv_obj_clear_flag(s_rail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_dro, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_content, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_flip_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_close_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_parent(s_safety, s_right);
        lv_obj_set_size(s_safety, CONTENT_W, SAFETY_H);
    }
}

// E4_POS_T stores positions as fixed point with 4 decimal places.
static void fmt_pos(char* out, size_t n, pos_t v) {
    snprintf(out, n, "%s", e4_to_cstr(v, 3));
}

void shell_tick() {
    if (model_take_state() || take_redisplay()) {
        lv_label_set_text(s_state_lbl, state_word(state));
        lv_obj_set_style_bg_color(s_state_chip, state_colour(state), 0);
        lv_obj_set_style_text_color(s_state_lbl, state == Disconnected ? C_DIM : C_BG, 0);
    }
    if (model_take_dro()) {
        char b[32];
        int n = n_axes < 3 ? n_axes : 3;
        for (int a = 0; a < n; a++) {
            fmt_pos(b, sizeof(b), myAxes[a]);
            lv_label_set_text(s_axis_work[a], b);
            char m[40];
            snprintf(m, sizeof(m), "M %s", e4_to_cstr(myAxes[a], 3));
            lv_label_set_text(s_axis_mach[a], m);
        }
    }
    if (offsets_dirty_take()) {
        lv_label_set_text(s_wcs_lbl, offsets_active());
    }

    switch (s_page) {
        case PAGE_JOG:     page_jog_tick();     break;
        case PAGE_OFFSETS: page_offsets_tick(); break;
        case PAGE_RUN:     page_run_tick();     break;
        default: break;
    }
    expanded_tick();
}
