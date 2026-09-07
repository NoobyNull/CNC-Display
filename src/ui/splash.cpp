#include "splash.h"
#include "theme.h"
#include "model/FluidNCModel.h"
#include "model/model_events.h"
#include "model/caps.h"
#include <lvgl.h>

// Boot splash: the FluidNC wordmark fades in while its pieces settle into
// place — arriving from behind the viewer — holds a beat, then the whole
// plate slides up and off, revealing the console that built underneath.
//
// This replaced a gantry-sweep reveal (per Matthew, 2026-09-07): the sweep
// invalidated a full-height region every frame and slideshowed on the real
// panel. Zoom+fade touches only the mark's own centred box per frame, the
// cheapest animation this display can draw.
//
// Everything lives on lv_layer_top() so it needs nothing from the shell and
// the shell needs nothing from it.

static lv_obj_t* s_root     = nullptr;
static lv_obj_t* s_mark     = nullptr;   // layout box for the wordmark
static lv_obj_t* s_leaf[3]  = {};        // word, sub, rule — animated each:
                                         // v8 opa/transform on a CONTAINER
                                         // does not cascade to children (it
                                         // shipped that way once: pop-in)
static lv_obj_t* s_status   = nullptr;   // what boot is waiting on

static const int ARRIVE_MS = 700;
static const int SPREAD    = U * 4;      // how far the leaves start from home
static const int HOLD_MS  = 1200;
static const int EXIT_MS  = 500;
static const int DELAY_MS = 150;         // timeline starts after boot, tiny settle only
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

// Polled rather than event-driven: the three things being waited on (link up,
// first report, timeout) are already polled elsewhere, and an observer registry
// for one consumer would be the only one in the codebase.
static void gate_cb(lv_timer_t*) {
    if (!s_root || !s_status) return;

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

static void hold_then_exit_cb(lv_anim_t*) {
    if (!s_root) return;

    // Already know the machine: keep the original timing exactly.
    if (model_dro_ever() && caps_settled()) { start_exit(HOLD_MS); return; }

    s_gate_start = lv_tick_get();
    s_gate = lv_timer_create(gate_cb, 100, nullptr);
}

// The arrival, without the transform engine: v8's transform_zoom on labels
// simply blanks them while zoomed (measured in the sim — mark invisible for
// the whole animation, popping in at the end; LV_DRAW_COMPLEX made no
// difference). So depth is faked with what provably renders: the word
// drifts down into place, the subtitle drifts up, the rule grows outward.
// v runs 0 (scattered) .. 100 (home).
static void settle_exec_cb(void*, int32_t v) {
    const lv_coord_t d = (lv_coord_t)(SPREAD * (100 - v) / 100);
    lv_obj_align(s_leaf[0], LV_ALIGN_CENTER, 0, (lv_coord_t)(-U * 2 - d));
    lv_obj_align(s_leaf[1], LV_ALIGN_CENTER, 0, (lv_coord_t)(U * 2 + d));
    lv_obj_set_width(s_leaf[2], (lv_coord_t)(U * 10 + U * 20 * v / 100));
    lv_obj_align(s_leaf[2], LV_ALIGN_CENTER, 0, 0);
}

static void fade_exec_cb(void*, int32_t v) {
    for (int i = 0; i < 3; i++)
        if (s_leaf[i]) lv_obj_set_style_opa(s_leaf[i], (lv_opa_t)v, 0);
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

    // Fade rides each leaf's own opa — the only opa v8 honours — and the
    // arrival starts scattered.
    s_leaf[0] = word; s_leaf[1] = sub; s_leaf[2] = rule;
    for (int i = 0; i < 3; i++)
        lv_obj_set_style_opa(s_leaf[i], LV_OPA_TRANSP, 0);
    settle_exec_cb(nullptr, 0);

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

    // Arrival from behind the viewer: oversized and transparent, settling to
    // life size as it becomes solid. Both anims are bound to s_mark so a
    // tap-delete of the splash kills them with it; unbound, they would keep
    // driving freed objects.
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_mark);
    lv_anim_set_exec_cb(&a, settle_exec_cb);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, ARRIVE_MS);
    lv_anim_set_delay(&a, DELAY_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, hold_then_exit_cb);
    lv_anim_start(&a);

    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, s_mark);
    lv_anim_set_exec_cb(&b, fade_exec_cb);
    lv_anim_set_values(&b, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&b, ARRIVE_MS);
    lv_anim_set_delay(&b, DELAY_MS);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_out);
    lv_anim_start(&b);
}
