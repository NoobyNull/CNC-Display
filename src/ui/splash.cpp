#include "splash.h"
#include "theme.h"
#include "model/FluidNCModel.h"
#include "model/model_events.h"
#include "model/caps.h"
#include <lvgl.h>

// Boot splash: the FluidNC wordmark, shown at once, with a progress bar that
// tracks what boot is actually waiting on. No entrance animation — per
// Matthew after two rounds of them fighting this panel's redraw budget:
// "forget the fancy shit, just load the screen and give a progress
// indicator." When the gate clears, the plate slides up and off.
//
// Everything lives on lv_layer_top() so it needs nothing from the shell and
// the shell needs nothing from it.

static lv_obj_t* s_root     = nullptr;
static lv_obj_t* s_mark     = nullptr;   // layout box for the wordmark
static lv_obj_t* s_bar      = nullptr;   // boot-stage progress
static lv_obj_t* s_status   = nullptr;   // what boot is waiting on

static const int HOLD_MS  = 700;
static const int EXIT_MS  = 500;
static const lv_coord_t MARK_W = U * 56;
static const lv_coord_t MARK_H = U * 22;

// The splash doubles as the boot gate. The layout the console draws is decided
// by the controller — axis count sets the DRO's column count and its font size,
// and the configured limit/probe pins decide which Diag lamps exist — so
// revealing the UI before the first status report means drawing a 3-axis
// machine that may not be this machine, then reflowing it under the operator.
// Hold here until the controller has described itself.
//
// It is a HOLD, never a lock. A panel that will not leave its loading screen
// without a controller is a panel you cannot reach SETUP on — and SETUP owns
// the Wired UART toggle and hold-UPDATE-FW, which are the two ways out of a
// bad link or a bad image. So: a tap skips it, and it gives up on its own.
static const int GATE_TIMEOUT_MS = 12000;

static lv_timer_t* s_gate = nullptr;
static uint32_t    s_gate_start = 0;

static void gate_stop() {
    if (!s_gate) return;
    lv_timer_del(s_gate);
    s_gate = nullptr;
}

static void splash_delete() {
    if (!s_root) return;
    gate_stop();                         // must die first: it dereferences s_status
    lv_obj_del(s_root);                  // takes every child and their anims with it
    s_root = nullptr;
    s_status = nullptr;
}

bool splash_active() { return s_root != nullptr; }

static void exit_done_cb(lv_anim_t*) { splash_delete(); }

static void exit_exec_cb(void* obj, int32_t v) { lv_obj_set_y((lv_obj_t*)obj, v); }

static void start_exit(int delay_ms) {
    if (!s_root) return;
    gate_stop();
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_root);
    lv_anim_set_exec_cb(&a, exit_exec_cb);
    lv_anim_set_values(&a, 0, -LV_VER_RES);
    lv_anim_set_time(&a, EXIT_MS);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, exit_done_cb);
    lv_anim_start(&a);
}

static void bar_update();

// Polled rather than event-driven: the three things being waited on (link up,
// first report, timeout) are already polled elsewhere, and an observer registry
// for one consumer would be the only one in the codebase.
static void gate_cb(lv_timer_t*) {
    if (!s_root || !s_status) return;

    bar_update();
    if (model_dro_ever() && caps_settled()) {
        // The machine has described itself; the shell underneath has already
        // reflowed to its axis count. Reveal a correct UI, not a corrected one.
        lv_label_set_text(s_status, "");
        start_exit(0);
        return;
    }

    if (lv_tick_elaps(s_gate_start) > (uint32_t)GATE_TIMEOUT_MS) {
        // Give up rather than strand the operator. The console comes up on its
        // compiled defaults with NO LINK showing, which is the honest state.
        lv_label_set_text(s_status, "no controller - starting anyway");
        start_exit(700);
        return;
    }

    lv_label_set_text(s_status, state == Disconnected
                                    ? "waiting for controller - tap to skip"
                                    : "reading machine config - tap to skip");
}

// Honest progress: each stage the boot actually passes moves the bar, and
// nothing creeps on its own. 25 = code running, 50 = controller talking,
// 75 = first status report, 100 = config settled.
static void bar_update() {
    if (!s_bar) return;
    int v = 25;
    if (state != Disconnected)  v = 50;
    if (model_dro_ever())       v = 75;
    if (caps_settled())         v = 100;
    lv_bar_set_value(s_bar, v, LV_ANIM_ON);
}

static void hold_then_exit() {
    if (!s_root) return;

    // Already know the machine: straight to the reveal.
    if (model_dro_ever() && caps_settled()) {
        bar_update();
        start_exit(HOLD_MS);
        return;
    }

    s_gate_start = lv_tick_get();
    s_gate = lv_timer_create(gate_cb, 100, nullptr);
}

static void tap_cb(lv_event_t*) { splash_delete(); }

void splash_show() {
    if (s_root) return;
    const int W = LV_HOR_RES, H = LV_VER_RES;

    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, W, H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, C_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_root, tap_cb, LV_EVENT_CLICKED, nullptr);

    // The wordmark block, zoomed and faded as one unit. NOT clickable —
    // lv_obj defaults to CLICKABLE, and a cosmetic child sitting over s_root
    // swallows the tap-to-skip (that exact bug shipped once; see ee2f716).
    // Starts invisible and oversized; splash_begin() flies it in.
    s_mark = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_mark);
    lv_obj_clear_flag(s_mark, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_mark, MARK_W, MARK_H);
    lv_obj_align(s_mark, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* word = lv_label_create(s_mark);
    lv_label_set_text(word, "FluidNC");
    lv_obj_set_style_text_font(word, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(word, C_TEXT, 0);
    lv_obj_align(word, LV_ALIGN_CENTER, 0, -U * 2);

    lv_obj_t* sub = lv_label_create(s_mark);
    lv_label_set_text(sub, "C N C   C O N S O L E");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, C_DIM, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, U * 2);

    lv_obj_t* rule = lv_obj_create(s_mark);
    lv_obj_remove_style_all(rule);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(rule, U * 30, 2);
    lv_obj_align(rule, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(rule, C_LINE, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    (void)word; (void)sub;

    // The progress bar, under the mark. lv_bar's own value animation gives
    // the smoothing; the stages give the truth.
    s_bar = lv_bar_create(s_root);
    lv_obj_set_size(s_bar, U * 30, U / 2);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, U * 10);
    lv_obj_set_style_bg_color(s_bar, C_RAISE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 2, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 25, LV_ANIM_OFF);   // code is running: stage one

    // Status sits on s_root rather than inside the reveal clip: it has to stay
    // readable while the plate slides, and it is boot information, not part of
    // the wordmark being machined out.
    s_status = lv_label_create(s_root);
    lv_label_set_text(s_status, "");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, C_DIM, 0);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -U * 4);

}

void splash_begin() {
    if (!s_root) return;                 // skipped by a tap during boot
    bar_update();
    hold_then_exit();
}
