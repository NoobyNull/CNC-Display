// Desktop stand-ins for src/hw/*: an SDL window instead of the RGB panel, the
// mouse instead of the GT911, and honest "not fitted" answers for the STC8,
// the IMU and the OTA updater.
//
// Every function here matches a declaration in the real src/hw headers, which
// are included rather than copied — if a hardware interface changes, this file
// stops compiling instead of quietly drifting.

#include <SDL2/SDL.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "hw/display_hw.h"
#include "hw/touch.h"
#include "hw/imu.h"
#include "hw/stc8.h"
#include "hw/fw_update.h"
#include "model/settings.h"

static SDL_Window*   s_win = nullptr;
static SDL_Renderer* s_ren = nullptr;
static SDL_Texture*  s_tex = nullptr;

static uint32_t* s_argb = nullptr;   // conversion target for SDL
static uint8_t   s_backlight = 200;
static bool      s_blank = false;

// Live pointer state, written by the SDL event pump in touch_poll().
static int16_t s_x = 0, s_y = 0;
static bool    s_down = false;
static uint32_t s_fresh = 0, s_polls = 0;

bool sim_should_quit = false;

// --- display ---------------------------------------------------------------

static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    // Convert through lv_color_to32() rather than handing SDL the raw 16-bit
    // buffer: that keeps this correct for whatever LV_COLOR_DEPTH and
    // LV_COLOR_16_SWAP src/lv_conf.h is set to, instead of encoding today's
    // values into a texture format here.
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            s_argb[y * w + x] = lv_color_to32(px[y * w + x]);
        }
    }

    SDL_Rect r = { area->x1, area->y1, w, h };
    SDL_UpdateTexture(s_tex, &r, s_argb, w * (int)sizeof(uint32_t));

    if (lv_disp_flush_is_last(drv)) {
        SDL_RenderClear(s_ren);
        SDL_RenderCopy(s_ren, s_tex, nullptr, nullptr);
        SDL_RenderPresent(s_ren);
    }
    lv_disp_flush_ready(drv);
}

void display_init() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        exit(1);
    }
    s_win = SDL_CreateWindow("CNC Console  -  simulated panel",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_RESIZABLE);
    s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED);
    // Logical size keeps the 800x480 panel geometry authoritative: resizing the
    // window scales the image instead of handing the UI a different resolution
    // than the hardware has.
    SDL_RenderSetLogicalSize(s_ren, SCREEN_WIDTH, SCREEN_HEIGHT);
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    s_argb = (uint32_t*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
}

void lvgl_init() {
    lv_init();

    // Same two-buffer, 1/10th-screen arrangement the panel uses, so the
    // simulator exercises the same partial-redraw paths as the hardware.
    static lv_disp_draw_buf_t db;
    static lv_color_t* b1 = (lv_color_t*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT / 10 * sizeof(lv_color_t));
    static lv_color_t* b2 = (lv_color_t*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT / 10 * sizeof(lv_color_t));
    lv_disp_draw_buf_init(&db, b1, b2, SCREEN_WIDTH * SCREEN_HEIGHT / 10);

    static lv_disp_drv_t dd;
    lv_disp_drv_init(&dd);
    dd.hor_res  = SCREEN_WIDTH;
    dd.ver_res  = SCREEN_HEIGHT;
    dd.flush_cb = flush_cb;
    dd.draw_buf = &db;
    lv_disp_drv_register(&dd);

    static lv_indev_drv_t id;
    lv_indev_drv_init(&id);
    id.type    = LV_INDEV_TYPE_POINTER;
    id.read_cb = touch_read_cb;
    lv_indev_drv_register(&id);
}

void lvgl_tick() { lv_timer_handler(); }
void display_resync() {}

// Dump what is on the glass right now. Works under SDL_VIDEODRIVER=dummy, so
// the UI can be inspected from a terminal or a CI job with no desktop at all —
// that is the whole point of it, and it is how UI changes get reviewed without
// a panel on the bench.
bool sim_screenshot(const char* path) {
    if (!s_ren || !path) return false;
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_WIDTH, SCREEN_HEIGHT,
                                                       32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return false;
    // Read back from the texture rather than the renderer: with the dummy
    // video driver the render target is never presented, so RenderReadPixels
    // would hand back an empty frame.
    SDL_SetRenderTarget(s_ren, nullptr);
    SDL_RenderClear(s_ren);
    SDL_RenderCopy(s_ren, s_tex, nullptr, nullptr);
    int rc = SDL_RenderReadPixels(s_ren, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                  surf->pixels, surf->pitch);
    bool ok = (rc == 0) && (SDL_SaveBMP(surf, path) == 0);
    SDL_FreeSurface(surf);
    if (!ok) fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
    return ok;
}

void set_backlight(uint8_t level) { s_backlight = level; stc8_backlight(level); }
uint8_t get_backlight() { return s_backlight; }

// Mirrors the firmware's idle blanking so the screensaver logic is testable:
// the window dims rather than going black, because a black SDL window reads as
// a crashed simulator.
void screen_idle_tick(bool machine_busy) {
    uint8_t mins = settings_screen_off_min();
    if (mins == 0 || machine_busy) {
        if (s_blank) { s_blank = false; stc8_display(true); }
        return;
    }
    bool idle = lv_disp_get_inactive_time(NULL) > (uint32_t)mins * 60000u;
    if (idle != s_blank) {
        s_blank = idle;
        stc8_display(!idle);
    }
}

// --- touch (SDL mouse) -----------------------------------------------------

void touch_init() {}
void gt911_reset() {}
void gt911_discover() {}
void gt911_dump_config() {}
void i2c_bus_recover() {}
bool i2c_take(uint32_t) { return true; }
void i2c_give() {}

void touch_poll() {
    s_polls++;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                sim_should_quit = true;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) { s_down = true; s_fresh++; }
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) s_down = false;
                break;
            case SDL_MOUSEMOTION: {
                // Window pixels are not panel pixels once the window is
                // resized; ask SDL to undo the logical-size scaling.
                float lx = 0, ly = 0;
                SDL_RenderWindowToLogical(s_ren, e.motion.x, e.motion.y, &lx, &ly);
                s_x = (int16_t)lx;
                s_y = (int16_t)ly;
                if (s_down) s_fresh++;
                break;
            }
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE) sim_should_quit = true;
                break;
        }
    }
}

void touch_read_cb(lv_indev_drv_t*, lv_indev_data_t* data) {
    data->point.x = s_x;
    data->point.y = s_y;
    data->state   = s_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool     touch_is_down()      { return s_down; }
uint32_t touch_fresh_count()  { return s_fresh; }
uint32_t touch_poll_count()   { return s_polls; }
uint32_t touch_lock_fails()   { return 0; }
uint32_t touch_stat_errs()    { return 0; }
uint8_t  touch_int_level()    { return s_down ? 0 : 1; }
uint8_t  touch_last_status()  { return s_down ? 0x81 : 0x00; }

// --- STC8 housekeeping MCU -------------------------------------------------

bool stc8_init() { return true; }
bool stc8_cmd(uint8_t) { return true; }

void stc8_backlight(uint8_t level) {
    // The real MCU dims the panel; here it tints the SDL window so blanking and
    // brightness changes are visible instead of invisible.
    if (s_ren) SDL_SetTextureColorMod(s_tex, level, level, level);
}

void stc8_display(bool on) { stc8_backlight(on ? s_backlight : 40); }

// --- IMU: none fitted ------------------------------------------------------
//
// The Advance 7.0 has no onboard IMU, so "absent" is the honest simulation and
// it keeps the vibration guard inert, exactly as on an un-modded panel.

bool  imu_begin()   { return false; }
bool  imu_retry()   { return false; }
bool  imu_present() { return false; }
void  imu_poll()    {}
float imu_last_magnitude() { return 0.0f; }
uint8_t imu_address() { return 0; }

// --- OTA -------------------------------------------------------------------

bool fwupd_net() { return false; }
