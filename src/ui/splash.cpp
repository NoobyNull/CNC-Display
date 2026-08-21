#include "splash.h"
#include "theme.h"
#include <lvgl.h>

// Boot splash: a gantry sweeps left-to-right and "machines" the FluidNC
// wordmark out of the dark, holds a beat, then the whole plate slides up and
// off, revealing the console that built underneath it in the meantime.
//
// Everything lives on lv_layer_top() so it needs nothing from the shell and
// the shell needs nothing from it; the sweep is one animation driving both
// the reveal clip and the gantry position, so they cannot drift apart.

static lv_obj_t* s_root     = nullptr;
static lv_obj_t* s_reveal   = nullptr;   // clipping window, width animated
static lv_obj_t* s_gantry   = nullptr;   // vertical line at the cut front
static lv_obj_t* s_carriage = nullptr;   // little block riding the gantry

static const int SWEEP_MS = 2800;
static const int HOLD_MS  = 1200;
static const int EXIT_MS  = 500;
static const int DELAY_MS = 150;         // timeline starts after boot, tiny settle only

static void splash_delete() {
    if (!s_root) return;
    lv_obj_del(s_root);                  // takes every child and their anims with it
    s_root = nullptr;
}

static void exit_done_cb(lv_anim_t*) { splash_delete(); }

static void exit_exec_cb(void* obj, int32_t v) { lv_obj_set_y((lv_obj_t*)obj, v); }

static void hold_then_exit_cb(lv_anim_t*) {
    if (!s_root) return;
    lv_obj_add_flag(s_gantry, LV_OBJ_FLAG_HIDDEN);   // cut done, tool retracts
    lv_obj_add_flag(s_carriage, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_root);
    lv_anim_set_exec_cb(&a, exit_exec_cb);
    lv_anim_set_values(&a, 0, -LV_VER_RES);
    lv_anim_set_time(&a, EXIT_MS);
    lv_anim_set_delay(&a, HOLD_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, exit_done_cb);
    lv_anim_start(&a);
}

static void sweep_exec_cb(void*, int32_t v) {
    lv_obj_set_width(s_reveal, v);
    lv_obj_set_x(s_gantry, v);
    lv_obj_set_x(s_carriage, v - 5);
}

static void carriage_exec_cb(void* obj, int32_t v) { lv_obj_set_y((lv_obj_t*)obj, v); }

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

    // Clipping window. Children are clipped to its bounds, so a fixed-size
    // plate inside it holds the text still while the window grows over it.
    s_reveal = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_reveal);
    lv_obj_set_size(s_reveal, 1, H);
    lv_obj_set_pos(s_reveal, 0, 0);

    lv_obj_t* plate = lv_obj_create(s_reveal);
    lv_obj_remove_style_all(plate);
    lv_obj_set_size(plate, W, H);
    lv_obj_set_pos(plate, 0, 0);

    lv_obj_t* word = lv_label_create(plate);
    lv_label_set_text(word, "FluidNC");
    lv_obj_set_style_text_font(word, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(word, C_TEXT, 0);
    lv_obj_align(word, LV_ALIGN_CENTER, 0, -U * 2);

    lv_obj_t* sub = lv_label_create(plate);
    lv_label_set_text(sub, "C N C   C O N S O L E");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, C_DIM, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, U * 2);

    lv_obj_t* rule = lv_obj_create(plate);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, U * 30, 2);
    lv_obj_align(rule, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(rule, C_LINE, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    // The gantry: a vertical accent line at the cut front, carriage riding it.
    s_gantry = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_gantry);
    lv_obj_set_size(s_gantry, 3, H);
    lv_obj_set_pos(s_gantry, 0, 0);
    lv_obj_set_style_bg_color(s_gantry, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(s_gantry, LV_OPA_COVER, 0);

    s_carriage = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_carriage);
    lv_obj_set_size(s_carriage, 13, 13);
    lv_obj_set_pos(s_carriage, -5, H / 2);
    lv_obj_set_style_bg_color(s_carriage, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(s_carriage, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_carriage, 2, 0);

}

void splash_begin() {
    if (!s_root) return;                 // skipped by a tap during boot

    // The cut: one animation drives reveal width, gantry and carriage x.
    lv_anim_t a;
    lv_anim_init(&a);
    // Bound to s_reveal so a tap-delete of the splash kills this anim with it;
    // unbound, it would keep calling sweep_exec_cb against freed objects.
    lv_anim_set_var(&a, s_reveal);
    lv_anim_set_exec_cb(&a, sweep_exec_cb);
    lv_anim_set_values(&a, 1, LV_HOR_RES);
    lv_anim_set_time(&a, SWEEP_MS);
    lv_anim_set_delay(&a, DELAY_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&a, hold_then_exit_cb);
    lv_anim_start(&a);

    // The carriage works the material: bobs along the gantry the whole sweep.
    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, s_carriage);
    lv_anim_set_exec_cb(&b, carriage_exec_cb);
    lv_anim_set_values(&b, LV_VER_RES / 2 - U * 6, LV_VER_RES / 2 + U * 6);
    lv_anim_set_time(&b, 350);
    lv_anim_set_playback_time(&b, 350);
    lv_anim_set_repeat_count(&b, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_in_out);
    lv_anim_start(&b);
}
