// Second window: the hardware chain the panel is talking to.
//
//   [ DISPLAY ] ──UART──► [ AIREDALE ] ──► [ CORGI ]
//
// This is DEV SCAFFOLDING and lives entirely in src/sim. It is not a page on
// the panel and never ships: the operator at the machine wants the machine, not
// a bus inventory. It exists so the display can be exercised as though it were
// wired to real boards, with the state of each box visible beside it.
//
// It renders on a SECOND LVGL display bound to a second SDL window, which is
// why it can reuse the panel's own theme and fonts rather than hand-rolling
// text out of SDL primitives.

#include <SDL2/SDL.h>
#include <lvgl.h>

#include "ui/theme.h"
#include "hw/display_hw.h"
#include "hw/stc8.h"
#include "hw/touch.h"
#include "hw/sdcard.h"
#include "hw/imu.h"
#include "model/FluidNCModel.h"
#include "model/model_events.h"
#include "model/caps.h"
#include "transport/transport.h"
#include "transport/uart.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

bool sim_link_is_live();

// Its own geometry: this is a desktop window, not the 800x480 panel, so it must
// not inherit the panel's constants.
static const int DEV_W = 520;
static const int DEV_H = 620;

static SDL_Window*   s_win = nullptr;
static SDL_Renderer* s_ren = nullptr;
static SDL_Texture*  s_tex = nullptr;
static uint16_t      s_fb[DEV_W * DEV_H];
static lv_disp_t*    s_disp = nullptr;

static void dev_flush(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) {
            if (x >= 0 && x < DEV_W && y >= 0 && y < DEV_H) s_fb[y * DEV_W + x] = px->full;
            px++;
        }
    lv_disp_flush_ready(drv);
}

// ---- one box per board -----------------------------------------------------
enum lamp_t { LAMP_UNKNOWN = 0, LAMP_GOOD, LAMP_WARN, LAMP_BAD };

static lv_color_t lamp_colour(lamp_t s) {
    switch (s) {
        case LAMP_GOOD: return C_OK;
        case LAMP_WARN: return C_WARN;
        case LAMP_BAD:  return C_CRIT;
        default:        return C_LINE;
    }
}

#define DEV_BOXES 3
#define DEV_ROWS  7

struct row_t { lv_obj_t* line; lv_obj_t* dot; lv_obj_t* key; lv_obj_t* val; };

static lv_obj_t* s_box[DEV_BOXES];
static row_t     s_row[DEV_BOXES][DEV_ROWS];

static const char* BOX_TITLE[DEV_BOXES] = { "DISPLAY", "AIREDALE", "CORGI" };
static const char* BOX_ROLE[DEV_BOXES]  = { "this panel", "passthrough", "FluidNC" };

static void build_row(lv_obj_t* parent, row_t* r) {
    r->line = lv_obj_create(parent);
    lv_obj_remove_style_all(r->line);
    lv_obj_set_size(r->line, LV_PCT(100), 22);
    lv_obj_set_flex_flow(r->line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r->line, 8, 0);

    r->dot = lv_obj_create(r->line);
    lv_obj_remove_style_all(r->dot);
    lv_obj_set_size(r->dot, 8, 8);
    lv_obj_set_style_radius(r->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(r->dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r->dot, C_LINE, 0);

    r->key = lv_label_create(r->line);
    lv_label_set_text(r->key, "");
    lv_obj_set_style_text_font(r->key, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(r->key, C_DIM, 0);
    lv_obj_set_width(r->key, 78);

    r->val = lv_label_create(r->line);
    lv_label_set_text(r->val, "");
    lv_obj_set_style_text_font(r->val, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(r->val, C_TEXT, 0);
    lv_obj_set_flex_grow(r->val, 1);
    lv_label_set_long_mode(r->val, LV_LABEL_LONG_CLIP);
}

static void set_row(int b, int i, lamp_t s, const char* key, const char* val) {
    row_t* r = &s_row[b][i];
    lv_obj_set_style_bg_color(r->dot, lamp_colour(s), 0);
    lv_label_set_text(r->key, key);
    lv_label_set_text(r->val, val);
    lv_obj_set_style_text_color(r->val, s == LAMP_UNKNOWN ? C_DIM : C_TEXT, 0);
    lv_obj_clear_flag(r->line, LV_OBJ_FLAG_HIDDEN);
}

static void hide_from(int b, int first) {
    for (int i = first; i < DEV_ROWS; i++) lv_obj_add_flag(s_row[b][i].line, LV_OBJ_FLAG_HIDDEN);
}

// A labelled connector between two boxes, so the window reads as a chain
// rather than as three unrelated panels.
static void build_link_arrow(lv_obj_t* parent, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(l, C_ACCENT, 0);
}

static void build_ui() {
    lv_obj_t* scr = lv_disp_get_scr_act(s_disp);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(scr, 12, 0);
    lv_obj_set_style_pad_row(scr, 4, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* t = lv_label_create(scr);
    lv_label_set_text(t, "HARDWARE  (simulator only)");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t, C_DIM, 0);

    for (int b = 0; b < DEV_BOXES; b++) {
        if (b) build_link_arrow(scr, b == 1 ? "|  UART 1M" : "|  pendant RJ12");

        s_box[b] = lv_obj_create(scr);
        lv_obj_set_width(s_box[b], LV_PCT(100));
        lv_obj_set_height(s_box[b], LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(s_box[b], C_SURFACE, 0);
        lv_obj_set_style_bg_opa(s_box[b], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_box[b], C_LINE, 0);
        lv_obj_set_style_border_width(s_box[b], 1, 0);
        lv_obj_set_style_radius(s_box[b], 3, 0);
        lv_obj_set_style_pad_all(s_box[b], 8, 0);
        lv_obj_set_style_pad_row(s_box[b], 0, 0);
        lv_obj_set_flex_flow(s_box[b], LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(s_box[b], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* h = lv_label_create(s_box[b]);
        lv_label_set_text(h, BOX_TITLE[b]);
        lv_obj_set_style_text_font(h, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(h, C_ACCENT, 0);

        lv_obj_t* sub = lv_label_create(s_box[b]);
        lv_label_set_text(sub, BOX_ROLE[b]);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(sub, C_DIM, 0);

        for (int i = 0; i < DEV_ROWS; i++) build_row(s_box[b], &s_row[b][i]);
    }
}

// ---- live values -----------------------------------------------------------
static const char* cap_word(cap_state_t c) {
    return c == CAP_PRESENT ? "yes" : c == CAP_ABSENT ? "no" : "unknown";
}

static lamp_t cap_lamp(cap_state_t c) {
    return c == CAP_PRESENT ? LAMP_GOOD : c == CAP_ABSENT ? LAMP_BAD : LAMP_UNKNOWN;
}

// main's caps model has no caps_macros(): derive it — settled with slots is
// present, settled without is absent, unsettled is unknown.
static cap_state_t caps_macros() {
    if (!caps_settled()) return CAP_UNKNOWN;
    return caps_macro_count() ? CAP_PRESENT : CAP_ABSENT;
}

static void tick_display() {
    char b[48];
    int  i = 0;
    set_row(0, i++, stc8_present() ? LAMP_GOOD : LAMP_BAD,
            "STC8", stc8_present() ? "0x30 ready" : "no answer (sim)");
    snprintf(b, sizeof(b), "%lu scans", (unsigned long)touch_fresh_count());
    set_row(0, i++, touch_fresh_count() ? LAMP_GOOD : LAMP_UNKNOWN, "TOUCH", b);
#ifdef HAS_SD_CARD
    set_row(0, i++, sd_present() ? LAMP_GOOD : LAMP_WARN,
            "SD", sd_present() ? "card present" : "no card");
#endif
    set_row(0, i++, imu_present() ? LAMP_GOOD : LAMP_UNKNOWN,
            "IMU", imu_present() ? "fitted" : "not fitted");
    snprintf(b, sizeof(b), "%u%%", (unsigned)((get_backlight() * 100) / 255));
    set_row(0, i++, LAMP_GOOD, "BACKLIGHT", b);
    set_row(0, i++, LAMP_GOOD, "FW", FW_TAG);
    hide_from(0, i);
}

static void tick_airedale() {
    char b[48];
    int  i = 0;
    // rx, not tx: bytes leaving prove nothing, bytes returning prove the whole
    // path came back through the passthrough.
    unsigned long rx = fnc_rx_count(), tx = fnc_tx_count();
    bool traffic = rx > 0 && tx > 0;
    snprintf(b, sizeof(b), "%lu tx / %lu rx", tx, rx);
    set_row(1, i++, traffic ? LAMP_GOOD : LAMP_BAD, "PASSTHRU", b);

    const char* board = model_expander_board();
    set_row(1, i++, board ? LAMP_GOOD : LAMP_UNKNOWN, "BOARD", board ? board : "no banner");
    set_row(1, i++, LAMP_UNKNOWN, "RJ12", "unprobeable");
    set_row(1, i++, LAMP_UNKNOWN, "5V", "own supply");
    hide_from(1, i);
}

static void tick_corgi() {
    char b[48];
    int  i = 0;
    bool up = state != Disconnected;
    set_row(2, i++, up ? LAMP_GOOD : LAMP_BAD, "STATE", up ? my_state_string : "disconnected");
    snprintf(b, sizeof(b), "%d", n_axes);
    set_row(2, i++, up ? LAMP_GOOD : LAMP_UNKNOWN, "AXES", b);
    set_row(2, i++, lastAlarm ? LAMP_BAD : LAMP_GOOD, "ALARM",
            lastAlarm ? "active" : "none");

    // The capability rows are the point of this window: they are what decides
    // which pages the panel next door is allowed to show.
    set_row(2, i++, cap_lamp(caps_probe()),  "PROBE",  cap_word(caps_probe()));
    set_row(2, i++, cap_lamp(caps_homing()), "HOMING", cap_word(caps_homing()));
    if (caps_macros() == CAP_PRESENT) snprintf(b, sizeof(b), "%u defined", (unsigned)caps_macro_count());
    else                          snprintf(b, sizeof(b), "%s", cap_word(caps_macros()));
    set_row(2, i++, cap_lamp(caps_macros()), "MACROS", b);
    set_row(2, i++, caps_settled() ? LAMP_GOOD : LAMP_UNKNOWN,
            "CONFIG", caps_settled() ? "$CD read" : "waiting");
    hide_from(2, i);
}

// ---- lifecycle -------------------------------------------------------------
bool devwin_open(int panel_w) {
    s_win = SDL_CreateWindow("CNC dev - hardware",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             DEV_W, DEV_H, SDL_WINDOW_RESIZABLE);
    if (!s_win) return false;

    // Sit beside the panel window rather than on top of it: this window is
    // meant to be read WHILE driving the display.
    int px, py;
    SDL_GetWindowPosition(s_win, &px, &py);
    SDL_SetWindowPosition(s_win, px + panel_w / 2 + 40, py);

    s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_SOFTWARE);
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_RGB565,
                              SDL_TEXTUREACCESS_STREAMING, DEV_W, DEV_H);

    static lv_disp_draw_buf_t buf;
    static lv_color_t px_buf[DEV_W * 40];
    lv_disp_draw_buf_init(&buf, px_buf, NULL, DEV_W * 40);

    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res  = DEV_W;
    drv.ver_res  = DEV_H;
    drv.flush_cb = dev_flush;
    drv.draw_buf = &buf;
    s_disp = lv_disp_drv_register(&drv);
    if (!s_disp) return false;

    // The panel's display must stay the default, or the shell's next
    // lv_layer_top() call would land on this window.
    lv_disp_t* panel = lv_disp_get_default();
    lv_disp_set_default(s_disp);
    build_ui();
    lv_disp_set_default(panel);
    return true;
}

uint32_t devwin_window_id() { return s_win ? SDL_GetWindowID(s_win) : 0; }

void devwin_close() {
    if (!s_win) return;
    SDL_DestroyWindow(s_win);
    s_win = nullptr;
}

void devwin_tick() {
    if (!s_win) return;
    tick_display();
    tick_airedale();
    tick_corgi();
}

void devwin_present() {
    if (!s_win) return;
    SDL_UpdateTexture(s_tex, NULL, s_fb, DEV_W * (int)sizeof(uint16_t));
    SDL_RenderClear(s_ren);
    SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    SDL_RenderPresent(s_ren);
}
