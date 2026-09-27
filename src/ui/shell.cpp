#include "ui/shell.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/view_expanded.h"
#include "model/fnc_codes.h"
#include "ui/goto_popup.h"
#include "model/FluidNCModel.h"
#include "model/model_events.h"
#include "model/caps.h"
#include "model/model_compat.h"
#include "model/offsets.h"
#include "model/settings.h"
#include "transport/transport.h"
#include "hw/touch.h"
#include "GrblParserC.h"
#include "e4math.h"
#include <stdio.h>

// Eight destinations, a five-item window. 5U + 5*10U + 5U = 60U, exactly the
// panel height, with every target at the full 10U. Fitting all eight at once
// would mean 7.5U items and a broken grid; a nested menu would cost a tap on
// things used constantly.
#define NAV_COUNT   10

// How many nav slots fit in the rail between the two scroll arrows. Derived from
// the panel rather than fixed: this was hardcoded to 5, which exactly filled the
// CrowPanel's 480px screen (40 + 5*80 + 40) and left 120px of dead rail on the
// 7B's 600px one. At 600px it yields 6; a taller panel gets more, and once a
// panel is tall enough for all eight the scroll arrows simply stay dimmed.
static const int NAV_FIT = (SCREEN_H - 2 * RAIL_SCROLL_H) / TOUCH_HOLD;
static const int NAV_VISIBLE = NAV_FIT > NAV_COUNT ? NAV_COUNT : NAV_FIT;

static_assert(NAV_VISIBLE >= 1, "rail is too short for a single nav slot");

static const char* NAV_LABEL[NAV_COUNT] = {
    "JOG", "HOME", "PROBE", "OFFSET", "RUN", "MACRO", "MOTION", "CAL", "DIAG", "SETUP"
};
static_assert(sizeof(NAV_LABEL) / sizeof(NAV_LABEL[0]) == PAGE_COUNT,
              "every page needs a rail label");

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
static lv_obj_t* s_link_lbl;
// FluidNC reports its axis count in every status report and the model already
// tracks it in n_axes; myAxes/myWco are sized 6. The DRO used to draw a fixed
// XYZ and clamp the count to 3, so a machine with a rotary A simply never showed
// it. Build all six and reveal the ones the controller says exist.
#define MAX_AXES 6
static lv_obj_t* s_axis_cell[MAX_AXES];
static lv_obj_t* s_axis_work[MAX_AXES];
static lv_obj_t* s_axis_mach[MAX_AXES];
static lv_obj_t* s_axis_dot[MAX_AXES];    // driver online: green dot
static lv_obj_t* s_axis_bang[MAX_AXES];   // driver dropped: red !
static int       s_axes_shown = 0;   // 0 = nothing laid out yet
static lv_obj_t* s_flip_btn;
static lv_obj_t* s_close_btn;
static lv_obj_t* s_ovr_box;
static lv_obj_t* s_ovr_val[3];
static lv_obj_t* s_ovr_fill[3];
static lv_obj_t* s_hold_btn;
static lv_obj_t* s_hold_lbl;
static lv_obj_t* s_unlock_btn;
static lv_obj_t* s_unlock_lbl;

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
// Which optional pages the rail shows is the operator's choice, set in
// Setup, not a deduction from the controller's config.
//
// This used to hide pages automatically from the config dump. It worked, but
// it made the rail change shape for reasons that were invisible from the
// panel: a page vanishing because a dump arrived slightly differently is not
// something anyone can debug standing at a machine. A checkbox is a thing you
// can look at. caps still drives the macro names, where the config genuinely
// is the only source.
static bool page_available(page_id_t id) {
    switch (id) {
        case PAGE_PROBE:
        case PAGE_MACRO:
        case PAGE_HOME:
        case PAGE_OFFSETS:
        case PAGE_CAL:   return settings_page_enabled((int)id);
        // JOG, RUN, SETUP and DIAG are not optional: jogging and running are
        // the job, and the other two are how a panel gets diagnosed.
        default:         return true;
    }
}

// Two rails, not one list.
//
// RUN mode is what an operator standing at a moving machine needs, and nothing
// else: jog, home, probe, run a file, fire a macro — each present only if this
// machine actually has it. CONFIG mode holds everything you set up once and
// then stop looking at: panel settings, work offsets, calibration, diagnostics.
//
// Mixing them cost rail slots that operation needed and put a settings screen
// one mistaken tap from a running job.
enum ui_mode_t { MODE_RUN = 0, MODE_CONFIG };

// -1 is the EXIT affordance rather than a page. Config mode must always show a
// way out on the glass: the gesture toggles both ways, but a gesture nobody
// remembers on a touch controller with a known stall is not an escape route.
#define NAV_EXIT (-1)

static const int RUN_PAGES[]    = { PAGE_JOG, PAGE_HOME, PAGE_PROBE, PAGE_RUN, PAGE_MACRO };
static const int CONFIG_PAGES[] = { PAGE_SETUP, PAGE_OFFSETS, PAGE_CAL, PAGE_MOTION, PAGE_DIAG, NAV_EXIT };

static const int RUN_N    = (int)(sizeof(RUN_PAGES) / sizeof(RUN_PAGES[0]));
static const int CONFIG_N = (int)(sizeof(CONFIG_PAGES) / sizeof(CONFIG_PAGES[0]));

static ui_mode_t s_mode = MODE_RUN;

// Rail slot -> page, rebuilt whenever the mode or capabilities change. Without
// this indirection the rail indexes pages directly and a hidden page leaves a
// hole in the middle of the strip.
static int s_visible[PAGE_COUNT + 1];
static int s_visible_n = 0;

static void nav_rebuild() {
    const int* src = s_mode == MODE_RUN ? RUN_PAGES : CONFIG_PAGES;
    const int  n   = s_mode == MODE_RUN ? RUN_N     : CONFIG_N;
    s_visible_n = 0;
    for (int i = 0; i < n; i++) {
        if (src[i] == NAV_EXIT || page_available((page_id_t)src[i]))
            s_visible[s_visible_n++] = src[i];
    }
}

static int nav_slot_of(page_id_t id) {
    for (int i = 0; i < s_visible_n; i++) if (s_visible[i] == (int)id) return i;
    return -1;
}

static void shell_set_mode(ui_mode_t m);

static int nav_max_start() {
    int m = s_visible_n - NAV_VISIBLE;
    return m > 0 ? m : 0;
}

static void nav_refresh() {
    if (s_nav_start > nav_max_start()) s_nav_start = nav_max_start();

    for (int i = 0; i < NAV_VISIBLE; i++) {
        int idx = s_nav_start + i;
        if (idx >= s_visible_n) {
            // Fewer pages than rail slots: blank the leftovers rather than
            // repeating the last label.
            lv_label_set_text(s_nav_lbl[i], "");
            lv_obj_set_style_bg_color(s_nav_btn[i], C_BG, 0);
            continue;
        }
        int pg = s_visible[idx];
        if (pg == NAV_EXIT) {
            lv_label_set_text(s_nav_lbl[i], "EXIT");
            lv_obj_set_style_bg_color(s_nav_btn[i], C_RAISE, 0);
            lv_obj_set_style_text_color(s_nav_lbl[i], C_WARN, 0);
            continue;
        }
        lv_label_set_text(s_nav_lbl[i], NAV_LABEL[pg]);
        bool on = (pg == (int)s_page);
        lv_obj_set_style_bg_color(s_nav_btn[i], on ? C_ACCENT : C_SURFACE, 0);
        lv_obj_set_style_text_color(s_nav_lbl[i], on ? C_BG : C_DIM, 0);
    }
    // Grey out at each end so the rail never lies about whether there is more.
    lv_obj_set_style_text_color(s_rail_up,
        s_nav_start > 0 ? C_DIM : C_LINE, 0);
    lv_obj_set_style_text_color(s_rail_down,
        s_nav_start < nav_max_start() ? C_DIM : C_LINE, 0);
}

static void nav_scroll_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int n = s_nav_start + dir;
    if (n < 0) n = 0;
    if (n > nav_max_start()) n = nav_max_start();
    s_nav_start = n;
    nav_refresh();
}

static void nav_pick_cb(lv_event_t* e) {
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    int idx  = s_nav_start + slot;
    if (idx < 0 || idx >= s_visible_n) return;   // blank slot
    if (s_visible[idx] == NAV_EXIT) { shell_set_mode(MODE_RUN); return; }
    shell_show_page((page_id_t)s_visible[idx]);
}

// Setup and Diag are the two pages with no business near a moving machine, so
// they alone trade the DRO's strip for content height. The safety bar stays on
// every page regardless.
static bool page_wants_dro(page_id_t id) {
    return id != PAGE_SETUP && id != PAGE_DIAG;
}

void shell_show_page(page_id_t id) {
    if (id < 0 || id >= PAGE_COUNT) return;
    if (s_page == PAGE_DIAG && id != PAGE_DIAG) page_diag_hidden();
    s_page = id;
    const bool dro = page_wants_dro(id);
    if (dro) lv_obj_clear_flag(s_dro, LV_OBJ_FLAG_HIDDEN);
    else     lv_obj_add_flag(s_dro, LV_OBJ_FLAG_HIDDEN);
    const lv_coord_t ch = dro ? CONTENT_H : CONTENT_H + DRO_H;
    lv_obj_set_height(s_content, ch);
    lv_obj_set_height(s_pages[id], ch - U * 2);
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (i == (int)id) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else              lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    // Keep the active item on screen when navigated to programmatically. The
    // rail scrolls in slot space, not page space, so the page has to be looked
    // up in the visible list first.
    int slot = nav_slot_of(id);
    if (slot >= 0) {
        if (slot < s_nav_start)                   s_nav_start = slot;
        if (slot > s_nav_start + NAV_VISIBLE - 1) s_nav_start = slot - NAV_VISIBLE + 1;
    }
    nav_refresh();
}

// Entering config mode lands on its first page; leaving returns to JOG, which
// is the page an operator wants when they put the panel back to work.
static void shell_set_mode(ui_mode_t m) {
    if (m == s_mode) return;
    s_mode = m;
    s_nav_start = 0;
    nav_rebuild();
    shell_show_page(m == MODE_CONFIG ? PAGE_SETUP : PAGE_JOG);
}

// Two fingers held anywhere opens or closes config mode. Held, not tapped: a
// palm or a knuckle brushing the glass registers two points for a moment, and
// that must not drop a running job's operator into a settings screen.
// Capabilities arrive after the link comes up, so the page set can change while
// the console is running. Called from the tick; cheap when nothing moved.
static void nav_sync_caps() {
    static int s_sig = -1;
    // Tracks the user's page settings, not the capability flags.
    int sig = 0;
    for (int i = 0; i < PAGE_COUNT; i++) sig |= (page_available((page_id_t)i) ? 1 : 0) << i;
    if (sig == s_sig) return;
    s_sig = sig;

    nav_rebuild();
    // If the open page just went away, do not leave the operator staring at a
    // page with no rail entry.
    if (nav_slot_of(s_page) < 0) shell_show_page(PAGE_JOG);
    else                         nav_refresh();
}

// ---- safety bar: on every screen, always the same place ---------------------
// One button for hold/resume: you cannot resume a machine that is not held,
// so the two are one control that reads whichever action is real right now.
static bool paused(state_t s) {
    return s == Hold || s == DoorOpen || s == DoorClosed;
}

static void hold_resume_cb(lv_event_t*) {
    fnc_realtime(paused(state) ? CycleStart : FeedHold);
}
static void reset_cb(lv_event_t*)  { fnc_realtime(Reset); }
static void flip_cb(lv_event_t*)   { expanded_flip(); }
static void close_cb(lv_event_t*)  { expanded_close(); }

// Three looks: RESUME green while paused, HOLD amber while there is motion to
// hold, HOLD dimmed when the machine is sitting still and the button is moot.
static void hold_resume_paint() {
    const bool r = paused(state);
    const bool live = r || state == Cycle || state == Jog || state == Homing;
    const lv_color_t bg = r ? C_OK : live ? C_WARN : C_RAISE;
    lv_label_set_text(s_hold_lbl, r ? "RESUME" : "HOLD");
    lv_obj_set_style_text_color(s_hold_lbl, live ? C_BG : C_DIM, 0);
    lv_obj_set_style_bg_color(s_hold_btn, bg, 0);
    lv_obj_set_style_bg_color(s_hold_btn, lv_color_lighten(bg, 40), LV_STATE_PRESSED);
}

// ---- override faders --------------------------------------------------------
// Each override tile is a vertical fader: press, drag up or down, release to
// send; long-press snaps back to 100%. The tile is only ~64px tall, so the drag
// is relative to where the finger landed and may run off the tile -- 1% per
// pixel gives the full 10..200% over a comfortable thumb stroke. Feed and
// spindle span FluidNC's 10..200% (Config.h FeedOverride); rapid only has
// three real settings, so it steps 25/50/100 every OVR_RAPID_PX.
static const int  OVR_MIN[3] = { 10, 0, 10 };
static const int  OVR_MAX[3] = { 200, 100, 200 };
static const int  OVR_DETENT = 4;         // +-% around 100 that snaps to 100
static const int  OVR_RAPID_PX = 30;      // drag per rapid step
static const uint32_t OVR_HOLD_MS = 1500; // show the sent value until Ov: catches up
static int      s_ovr_target[3] = { -1, -1, -1 };   // -1 = not being set
static int      s_ovr_start[3];                     // value under the finger at press
static lv_coord_t s_ovr_y0[3];
static bool     s_ovr_moved[3];                     // finger travelled: not a long-press
static const lv_coord_t OVR_STILL_PX = 8;           // travel under this is "held still"
static uint32_t s_ovr_sent_ms[3];

static int ovr_now(int i) {
    return i == 0 ? (int)myFro : i == 1 ? (int)myRro : (int)mySro;
}

static void ovr_fill(int i, int v) {
    int span = OVR_MAX[i] - OVR_MIN[i];
    int f = (v - OVR_MIN[i]) * 100 / span;
    lv_obj_set_height(s_ovr_fill[i], LV_PCT(f < 0 ? 0 : f > 100 ? 100 : f));
}

static void ovr_show(int i, int v, lv_color_t col) {
    char b[12];
    snprintf(b, sizeof(b), "%d%%", v);
    lv_label_set_text(s_ovr_val[i], b);
    lv_obj_set_style_text_color(s_ovr_val[i], col, 0);
    ovr_fill(i, v);
}

static int ovr_from_drag(int i, lv_coord_t dy) {   // dy > 0 = finger moved up
    if (i == 1) {
        static const int R[3] = { 25, 50, 100 };
        int k = s_ovr_start[i] >= 100 ? 2 : s_ovr_start[i] >= 50 ? 1 : 0;
        k += dy / OVR_RAPID_PX;
        return R[k < 0 ? 0 : k > 2 ? 2 : k];
    }
    int v = s_ovr_start[i] + dy;
    if (v < OVR_MIN[i]) v = OVR_MIN[i];
    if (v > OVR_MAX[i]) v = OVR_MAX[i];
    return (v > 100 - OVR_DETENT && v < 100 + OVR_DETENT) ? 100 : v;
}

// Reset first, then step from a known 100%: the result never depends on how
// stale the last Ov: report was. The bytes land in one burst, well inside a
// single planner tick, so the momentary 100% never reaches the cutter.
static void ovr_send(int i, int v) {
    if (i == 1) {
        fnc_realtime(v == 100 ? RapidOvrReset : v == 50 ? RapidOvrMedium : RapidOvrLow);
        return;
    }
    const realtime_cmd_t rst = i == 0 ? FeedOvrReset         : SpindleOvrReset;
    const realtime_cmd_t cp  = i == 0 ? FeedOvrCoarsePlus    : SpindleOvrCoarsePlus;
    const realtime_cmd_t cm  = i == 0 ? FeedOvrCoarseMinus   : SpindleOvrCoarseMinus;
    const realtime_cmd_t fp  = i == 0 ? FeedOvrFinePlus      : SpindleOvrFinePlus;
    const realtime_cmd_t fm  = i == 0 ? FeedOvrFineMinus     : SpindleOvrFineMinus;
    fnc_realtime(rst);
    int d = v - 100;
    for (; d >= 10; d -= 10)  fnc_realtime(cp);
    for (; d <= -10; d += 10) fnc_realtime(cm);
    for (; d > 0; d--)        fnc_realtime(fp);
    for (; d < 0; d++)        fnc_realtime(fm);
}

static void ovr_cb(lv_event_t* e) {
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);

    // A slow drag outlasts the long-press time too; only a finger that has
    // held still means "reset".
    if (code == LV_EVENT_LONG_PRESSED && !s_ovr_moved[i]) {
        s_ovr_target[i] = -1;          // the release must not send the drag value
        ovr_send(i, 100);
        ovr_show(i, 100, C_ACCENT);
        s_ovr_sent_ms[i] = lv_tick_get();
    } else if (code == LV_EVENT_PRESSED) {
        lv_point_t pt;
        lv_indev_get_point(lv_indev_get_act(), &pt);
        s_ovr_y0[i] = pt.y;
        s_ovr_start[i] = ovr_now(i);
        s_ovr_target[i] = s_ovr_start[i];
        s_ovr_moved[i] = false;
    } else if (code == LV_EVENT_PRESSING && s_ovr_target[i] >= 0) {
        lv_point_t pt;
        lv_indev_get_point(lv_indev_get_act(), &pt);
        const lv_coord_t dy = s_ovr_y0[i] - pt.y;
        if (LV_ABS(dy) >= OVR_STILL_PX) s_ovr_moved[i] = true;
        s_ovr_target[i] = ovr_from_drag(i, dy);
        ovr_show(i, s_ovr_target[i], C_ACCENT);
    } else if (code == LV_EVENT_RELEASED && s_ovr_target[i] >= 0) {
        // A plain tap (no travel) changes nothing and sends nothing.
        if (s_ovr_target[i] != s_ovr_start[i]) ovr_send(i, s_ovr_target[i]);
        s_ovr_target[i] = -1;
        s_ovr_sent_ms[i] = lv_tick_get();
    }
}

// $X clears the alarm lock. An alarm can land on any page, so this lives in
// the bar rather than only on HOME. Same three-look idea as HOLD: amber while
// the machine is in Alarm and the button is the way out, dim otherwise ($X on
// an idle machine is a harmless "ok", so it stays pressable).
static void unlock_cb(lv_event_t*) { send_line("$X"); }

static void unlock_paint() {
    const bool alarm = state == Alarm;
    const lv_color_t bg = alarm ? C_WARN : C_RAISE;
    lv_obj_set_style_text_color(s_unlock_lbl, alarm ? C_BG : C_DIM, 0);
    lv_obj_set_style_bg_color(s_unlock_btn, bg, 0);
    lv_obj_set_style_bg_color(s_unlock_btn, lv_color_lighten(bg, 40), LV_STATE_PRESSED);
}

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

    s_hold_btn = w_button(s_safety, "HOLD", nullptr, C_RAISE, C_DIM,
                          hold_resume_cb, nullptr);
    lv_obj_set_size(s_hold_btn, U * 19, LV_PCT(100));   // 152 x 64
    s_hold_lbl = lv_obj_get_child(lv_obj_get_child(s_hold_btn, 0), 0);

    lv_obj_t* rst = w_button(s_safety, "RESET", nullptr, C_CRIT, C_BG, reset_cb, nullptr);
    lv_obj_set_size(rst, U * 19, LV_PCT(100));

    s_unlock_btn = w_button(s_safety, "UNLOCK", "$X", C_RAISE, C_DIM, unlock_cb, nullptr);
    lv_obj_set_size(s_unlock_btn, U * 12, LV_PCT(100));
    s_unlock_lbl = lv_obj_get_child(lv_obj_get_child(s_unlock_btn, 0), 0);

    // Overrides fill the remainder in normal mode; FLIP/CLOSE replace them when
    // the expanded view is open.
    s_ovr_box = lv_obj_create(s_safety);
    w_flatten(s_ovr_box);
    lv_obj_set_flex_grow(s_ovr_box, 1);
    lv_obj_set_height(s_ovr_box, LV_PCT(100));
    lv_obj_set_flex_flow(s_ovr_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_ovr_box, U, 0);

    static const char* OVR_K[3] = { "FEED", "RAPID", "SPINDLE" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* cell = w_panel(s_ovr_box, C_RAISE);
        lv_obj_set_flex_grow(cell, 1);
        lv_obj_set_height(cell, LV_PCT(100));
        // A drag must stay with the fader, not scroll or swipe the page.
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_add_event_cb(cell, ovr_cb, LV_EVENT_ALL, (void*)(intptr_t)i);

        // Created before the labels so it draws behind them.
        s_ovr_fill[i] = lv_obj_create(cell);
        w_flatten(s_ovr_fill[i]);
        lv_obj_set_style_bg_color(s_ovr_fill[i], C_ACCENT, 0);
        lv_obj_set_style_bg_opa(s_ovr_fill[i], LV_OPA_30, 0);
        lv_obj_set_width(s_ovr_fill[i], LV_PCT(100));
        lv_obj_align(s_ovr_fill[i], LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_clear_flag(s_ovr_fill[i], LV_OBJ_FLAG_CLICKABLE);
        ovr_fill(i, 100);

        lv_obj_t* k = w_label(cell, OVR_K[i], &lv_font_montserrat_10, C_DIM);
        lv_obj_align(k, LV_ALIGN_CENTER, 0, -11);
        s_ovr_val[i] = w_label(cell, "100%", &lv_font_montserrat_16, C_TEXT);
        lv_obj_align(s_ovr_val[i], LV_ALIGN_CENTER, 0, 9);
    }

    s_flip_btn = w_button(s_safety, "FLIP", "view", C_ACCENT, C_BG, flip_cb, nullptr);
    lv_obj_set_size(s_flip_btn, U * 11, LV_PCT(100));
    lv_obj_add_flag(s_flip_btn, LV_OBJ_FLAG_HIDDEN);

    s_close_btn = w_button(s_safety, "CLOSE", "back", C_RAISE, C_TEXT, close_cb, nullptr);
    lv_obj_set_size(s_close_btn, U * 11, LV_PCT(100));
    lv_obj_add_flag(s_close_btn, LV_OBJ_FLAG_HIDDEN);
}

// ---- DRO -------------------------------------------------------------------
// Width left for the axis cells once the state/WCS column is taken out.
static const lv_coord_t AXES_W = CONTENT_W - U * 16;

// Axis count the DRO should show: what the controller reports, minus A when
// its driver is offline. With A's telemetry not built yet (M66 path,
// 2026-09-07 contract), drv_axis_ok('A') is always true and A never hides —
// the gate re-engages by itself once A reporting lands.
static int gated_axis_count() {
    int n = n_axes;
    if (n < 1)        n = 1;
    if (n > MAX_AXES) n = MAX_AXES;
    if (drv_feature_armed() && n >= 4 && !drv_axis_ok('A')) n = 3;
    return n;
}

// Reflow the DRO for n axes. The readout font has to come down as the cells
// narrow — a 40px "-1234.567" needs about 230px, so it fits three cells across
// and clips at four. Picked off the resulting cell width rather than the axis
// count so it stays right if the DRO or the panel is ever resized.
static void dro_layout(int n) {
    if (n < 1)         n = 1;
    if (n > MAX_AXES)  n = MAX_AXES;
    if (n == s_axes_shown) return;
    s_axes_shown = n;

    const lv_coord_t cw = AXES_W / n;
    const lv_font_t* f = cw >= U * 30 ? &lv_font_montserrat_40
                       : cw >= U * 22 ? &lv_font_montserrat_28
                                      : &lv_font_montserrat_20;

    for (int i = 0; i < MAX_AXES; i++) {
        if (i < n) {
            lv_obj_clear_flag(s_axis_cell[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_width(s_axis_cell[i], cw);
            // Divider between cells, but not trailing off the last one.
            lv_obj_set_style_border_width(s_axis_cell[i], i < n - 1 ? 1 : 0, 0);
            lv_obj_set_style_text_font(s_axis_work[i], f, 0);
        } else {
            lv_obj_add_flag(s_axis_cell[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

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
    // One tap into config mode (lands on Setup) from any running page. This
    // replaces the old two-finger hold outright: the GT911 driver reports a
    // second point too unreliably to hang a mode switch off it, and the chip
    // is on every DRO page. Setup hides the DRO, so it cannot re-fire from
    // inside; the rail's EXIT slot is the way back.
    lv_obj_add_flag(s_state_chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_state_chip, [](lv_event_t*) {
        shell_set_mode(MODE_CONFIG);
    }, LV_EVENT_CLICKED, nullptr);

    s_wcs_lbl = w_label(sc, "G54", &lv_font_montserrat_12, C_ACCENT);
    s_link_lbl = w_label(sc, "NO LINK", &lv_font_montserrat_10, C_DIM);

    lv_obj_t* axes = lv_obj_create(s_dro);
    w_flatten(axes);
    lv_obj_set_size(axes, AXES_W, LV_PCT(100));
    lv_obj_set_flex_flow(axes, LV_FLEX_FLOW_ROW);

    for (int i = 0; i < MAX_AXES; i++) {
        lv_obj_t* c = lv_obj_create(axes);
        w_flatten(c);
        lv_obj_set_height(c, LV_PCT(100));
        lv_obj_set_style_pad_left(c, U * 2, 0);
        lv_obj_set_style_border_side(c, LV_BORDER_SIDE_RIGHT, 0);
        lv_obj_set_style_border_color(c, C_LINE, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        // axisNumToChar() is the model's own "XYZABC" mapping — no second copy.
        char nm[2] = { axisNumToChar(i), 0 };
        w_label(c, nm, &lv_font_montserrat_12, C_DIM);
        s_axis_work[i] = w_label(c, "0.000", &lv_font_montserrat_40, C_TEXT);
        s_axis_mach[i] = w_label(c, "M 0.000", &lv_font_montserrat_12, C_DIM);

        // Driver-health badge floating in the cell's top-right corner: green
        // dot while every driver behind this axis reports online in Pn:,
        // red ! when one drops. Hidden until the wiring ever reports.
        lv_obj_t* dot = lv_obj_create(c);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, U, U);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, C_OK, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -U, U / 2);
        s_axis_dot[i] = dot;

        lv_obj_t* bang = w_label(c, "!", &lv_font_montserrat_16, C_CRIT);
        lv_obj_add_flag(bang, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(bang, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(bang, LV_ALIGN_TOP_RIGHT, -U, U / 4);
        s_axis_bang[i] = bang;
        // The readout doubles as the target picker: tap an axis to type an
        // absolute coordinate and jog there. The step selector caps at 10mm,
        // which made a long traverse dozens of presses.
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, [](lv_event_t* e) {
            goto_open((int)(intptr_t)lv_event_get_user_data(e));
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        s_axis_cell[i] = c;
    }
    dro_layout(3);   // until FluidNC says otherwise
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
        // TOUCH_HOLD is the floor, not the height: whatever the slot count does
        // not divide evenly is shared out here so the rail ends flush with the
        // bottom of the screen instead of leaving a stub of background.
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_height(b, TOUCH_HOLD);
        lv_obj_set_flex_grow(b, 1);
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
        page_run_build, page_macro_build, page_motion_build, page_cal_build,
        page_diag_build, page_setup_build
    };
    for (int i = 0; i < PAGE_COUNT; i++) {
        s_pages[i] = lv_obj_create(s_content);
        w_flatten(s_pages[i]);
        lv_obj_set_size(s_pages[i], CONTENT_W - U * 2, CONTENT_H - U * 2);
        lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        BUILD[i](s_pages[i]);
    }

    expanded_build();
    nav_rebuild();
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
        // The safety bar is never covered: the controls you hit in a hurry keep
        // their full height and the expanded view takes what is left.
        lv_obj_set_parent(s_safety, s_scr);
        lv_obj_set_pos(s_safety, 0, SCREEN_H - SAFETY_H);
        lv_obj_set_size(s_safety, SCREEN_W, SAFETY_H);
        lv_obj_move_foreground(s_safety);
    } else {
        lv_obj_clear_flag(s_rail, LV_OBJ_FLAG_HIDDEN);
        if (page_wants_dro(s_page)) lv_obj_clear_flag(s_dro, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_content, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_flip_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_close_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_parent(s_safety, s_right);
        lv_obj_set_size(s_safety, CONTENT_W, SAFETY_H);
    }
}

// Only a machine that is genuinely sitting still lets the panel go dark. Every
// alarm state counts as busy: an alarm you cannot see is an alarm nobody acts
// on. Disconnected counts as idle -- a panel with no controller answering has
// no reason to stay lit all night.
static bool machine_busy(state_t s) {
    switch (s) {
        case Idle:
        case Disconnected:
        case GrblSleep:   return false;
        default:          return true;
    }
}

// E4_POS_T stores positions as fixed point with 4 decimal places.
static void fmt_pos(char* out, size_t n, pos_t v) {
    snprintf(out, n, "%s", e4_to_cstr(v, 3));
}

void shell_tick() {
    // Recompute what the machine can do before anything paints, so a settings
    // or config dump that just landed is reflected on this frame.
    caps_poll();
    nav_sync_caps();
    // Both re-gate their own controls and are cheap no-ops until the
    // capability model actually changes, so they run regardless of which page
    // is on screen — a page must be right the moment it is shown.
    page_home_tick();
    page_probe_tick();
    if (model_take_state() || take_redisplay()) {
        // The alarm number rides on the chip; its name takes the link line
        // below, which says nothing useful while the machine is locked.
        char w[16];
        if (state == Alarm && lastAlarm > 0) snprintf(w, sizeof(w), "ALARM %d", lastAlarm);
        else                                 snprintf(w, sizeof(w), "%s", state_word(state));
        lv_label_set_text(s_state_lbl, w);
        lv_obj_set_style_bg_color(s_state_chip, state_colour(state), 0);
        lv_obj_set_style_text_color(s_state_lbl, state == Disconnected ? C_DIM : C_BG, 0);
        hold_resume_paint();
        unlock_paint();
    }
    if (model_take_dro()) {
        char b[32];
        // Follow the controller. A 4-axis machine reports 4 and the A column
        // appears on its own; dro_layout() returns early when nothing changed,
        // so this costs a compare per DRO update.
        int n = n_axes;
        if (n < 1)        n = 1;
        if (n > MAX_AXES) n = MAX_AXES;
        dro_layout(gated_axis_count());
        // Labels update for ALL reported axes, including a gated-out A: its
        // cell must be current the instant the driver returns.
        for (int a = 0; a < n; a++) {
            fmt_pos(b, sizeof(b), myAxes[a]);
            lv_label_set_text(s_axis_work[a], b);
            char m[44];
            snprintf(m, sizeof(m), "M %s", e4_to_cstr(myAxes[a] + myWco[a], 3));
            lv_label_set_text(s_axis_mach[a], m);
        }
    }
    const char* an = state == Alarm ? fnc_alarm_name(lastAlarm) : nullptr;
    lv_label_set_text(s_link_lbl, an ? an : transport_label());
    lv_obj_set_style_text_color(s_link_lbl, an ? C_WARN : C_DIM, 0);

    // Driver badges + the A gate, every tick: pin changes ride status
    // reports that may not move the DRO, and two string scans are cheap.
    {
        const bool armed = drv_feature_armed();
        const int  shown = gated_axis_count();
        dro_layout(shown);
        for (int i = 0; i < MAX_AXES; i++) {
            if (!s_axis_dot[i]) continue;
            const char ax = axisNumToChar(i);
            // A is absent: its status arrives via M66 polling (not Pn:),
            // which isn't built yet — no badge beats a lying green one.
            const bool mapped = armed && i < shown &&
                (ax == 'X' || ax == 'Y' || ax == 'Z');
            const bool ok = mapped && drv_axis_ok(ax);
            if (mapped && ok) lv_obj_clear_flag(s_axis_dot[i], LV_OBJ_FLAG_HIDDEN);
            else              lv_obj_add_flag(s_axis_dot[i], LV_OBJ_FLAG_HIDDEN);
            if (mapped && !ok) lv_obj_clear_flag(s_axis_bang[i], LV_OBJ_FLAG_HIDDEN);
            else               lv_obj_add_flag(s_axis_bang[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (offsets_dirty_take()) {
        lv_label_set_text(s_wcs_lbl, offsets_active());
    }

    // An override that is not 100% is tinted: a job cutting at 115% feed should
    // never look the same as one at 100%.
    const int ovr[3] = { (int)myFro, (int)myRro, (int)mySro };
    for (int i = 0; i < 3; i++) {
        // Hands off while the finger is on it, and briefly after a send until
        // the next Ov: report can reflect it -- otherwise the fader snaps back.
        if (s_ovr_target[i] >= 0 || lv_tick_elaps(s_ovr_sent_ms[i]) < OVR_HOLD_MS) continue;
        ovr_show(i, ovr[i], ovr[i] == 100 ? C_TEXT : C_WARN);
    }

    switch (s_page) {
        case PAGE_JOG:     page_jog_tick();     break;
        case PAGE_OFFSETS: page_offsets_tick(); break;
        case PAGE_RUN:     page_run_tick();     break;
        case PAGE_SETUP:   page_setup_tick();   break;
        case PAGE_DIAG:    page_diag_tick();    break;
        case PAGE_CAL:     page_cal_tick();     break;
        case PAGE_MACRO:   page_macro_tick();   break;
        case PAGE_MOTION:  page_motion_tick();  break;
        default: break;
    }
    expanded_tick();

    screen_idle_tick(machine_busy(state));
}
