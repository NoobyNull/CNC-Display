// Desktop simulator entry point for [env:sim].
//
// Runs the REAL src/ui and src/model against SDL instead of the RGB panel and
// the GT911. What you see in the window is what LVGL composites from the same
// widget tree, the same lv_conf.h and the same fonts the firmware uses; only
// the pixels' destination changed.
//
//   ./sim                     open a window
//   ./sim --shot out.bmp      render, write one frame, exit (works headless)
//
// Window keys:
//   1..5   force Idle / Run / Hold / Alarm / Jog on the fake controller
//   6      play the next tool change of the Snowman job (atc_manual timing)
//   space  freeze or resume the moving DRO position
//   right-drag  hold = two-finger press: toggle config mode
//   s      write shot.bmp
//   esc/q  quit

#include <SDL2/SDL.h>
#include <lvgl.h>

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "hw/display_hw.h"
#include "hw/sdcard.h"
#include "hw/imu.h"
#include "model/settings.h"
#include "model/console_log.h"
#include "model/offsets.h"
#include "model/filelist.h"
#include "model/path_loader.h"
#include "model/vibration.h"
#include "model/model_events.h"
#include "model/model_compat.h"
#include "model/caps.h"
#include "model/fnc_settings.h"
#include "model/FluidNCModel.h"
#include "transport/transport.h"
#include "transport/xmodem_rx.h"
#include "ui/shell.h"
#include "ui/splash.h"
#include "GrblParserC.h"

// From sim_transport.cpp / sim_hw.cpp.
void sim_transport_tick();
void sim_set_state(const char* st);
void sim_raise_alarm(int n);
void sim_tool_change(int t);
void sim_toggle_motion();
bool sim_link_open(const char* path);

// Second window (src/sim/sim_devwin.cpp): the hardware chain, dev-only.
bool     devwin_open(int panel_w);
void     devwin_tick();
void     devwin_present();
void     devwin_close();
uint32_t devwin_window_id();
bool sim_link_is_live();
extern bool     sim_touch_down;
extern uint32_t sim_touch_scans;
extern uint8_t  sim_touch_pts;

// ---------------- Arduino shim backing ----------------
SimSerial Serial;

static uint64_t s_epoch_ms = 0;

static uint64_t now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

uint32_t millis() { return (uint32_t)(now_ms() - s_epoch_ms); }

void delay(uint32_t ms) { SDL_Delay(ms); }

// ---------------- framebuffer ----------------
// LVGL flushes partial areas; they are composited here into a full-screen
// RGB565 image. Keeping our own copy is what lets a screenshot be written
// without a working video driver — the SDL texture is downstream of this, not
// the other way round.
static uint16_t s_fb[SCREEN_WIDTH * SCREEN_HEIGHT];

static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            s_fb[y * SCREEN_WIDTH + x] = px->full;
            px++;
        }
    }
    lv_disp_flush_ready(drv);
}

// 24-bit BMP, bottom-up. Deliberately not PNG: this is the only image format
// that needs no library at all, and the file is a temporary anyway.
static bool write_bmp(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    const int row = SCREEN_WIDTH * 3;
    const int pad = (4 - (row % 4)) % 4;
    const int img = (row + pad) * SCREEN_HEIGHT;

    uint8_t hdr[54] = { 0 };
    memcpy(hdr, "BM", 2);
    const uint32_t total = 54 + img;
    memcpy(hdr + 2, &total, 4);
    const uint32_t off = 54;
    memcpy(hdr + 10, &off, 4);
    const uint32_t dib = 40;
    memcpy(hdr + 14, &dib, 4);
    const int32_t w = SCREEN_WIDTH, h = SCREEN_HEIGHT;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    const uint16_t planes = 1, bpp = 24;
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &img, 4);
    fwrite(hdr, 1, sizeof(hdr), f);

    static const uint8_t zero[3] = { 0, 0, 0 };
    for (int y = SCREEN_HEIGHT - 1; y >= 0; y--) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            const uint16_t c = s_fb[y * SCREEN_WIDTH + x];
            // RGB565 -> 8 bits per channel, replicating the high bits into the
            // low ones so full-scale stays full-scale (0x1F -> 0xFF, not 0xF8).
            const uint8_t r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
            const uint8_t px[3] = { (uint8_t)((b5 << 3) | (b5 >> 2)),
                                    (uint8_t)((g6 << 2) | (g6 >> 4)),
                                    (uint8_t)((r5 << 3) | (r5 >> 2)) };
            fwrite(px, 1, 3, f);
        }
        if (pad) fwrite(zero, 1, pad, f);
    }
    fclose(f);
    return true;
}

// ---------------- touch ----------------
// The mouse stands in for a fingertip: button-down is contact. LVGL's own
// gesture and long-press handling is driver-independent, so what the panel
// does with a press is what this does with a click.
static int  s_px = 0, s_py = 0;
static bool s_pressed = false;

// Which page --shot captures. -1 leaves whatever the shell opened on.
static int s_shot_page = -1;

// --tap "x,y[;x,y>x2,y2...]": inject presses (or drags) before capturing. This is how input is
// regression-tested without a human and without a window manager — the same
// path a real press takes, driven from a script.
static const char* s_tap = nullptr;

// --gesture: hold two fingers before capturing, exercising the real gesture
// path rather than reaching into the shell's mode directly.
static bool s_gesture = false;

// --alarm N: canned fake only; raised after settle, before any taps.
static int s_alarm = 0;
static int s_tool = 0;   // --toolchange N: canned fake pauses for tool N

// Hold long enough that LVGL's indev timer (LV_INDEV_DEF_READ_PERIOD, 30ms)
// cannot miss the press, then release and let the click event dispatch.
static const uint32_t TAP_HOLD_MS    = 120;
static const uint32_t TAP_RELEASE_MS = 120;

// A press is latched until LVGL has actually sampled it once. The indev timer
// runs at LV_INDEV_DEF_READ_PERIOD (30ms); a quick trackpad tap can go down and
// up inside that window, and without the latch the click would never exist as
// far as LVGL is concerned. The touch driver on the panel latches for exactly
// this reason — see the GT911 notes in docs/HANDOFF.md.
static bool s_latched = false;

static void indev_cb(lv_indev_drv_t*, lv_indev_data_t* data) {
    data->point.x = s_px;
    data->point.y = s_py;
    if (s_pressed || s_latched) {
        data->state = LV_INDEV_STATE_PRESSED;
        s_latched   = false;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ---------------- boot ----------------
// Mirrors setup() in main.cpp, minus the parts that address hardware. Order
// matters for the same reasons it matters there: settings before anything
// reads them, splash painted before the slow work, shell built last.
static void boot() {
    lv_init();

    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf[SCREEN_WIDTH * 60];
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, SCREEN_WIDTH * 60);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = SCREEN_WIDTH;
    disp_drv.ver_res  = SCREEN_HEIGHT;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = indev_cb;
    lv_indev_drv_register(&indev_drv);

    settings_load();

    splash_show();
    lv_timer_handler();

    clog_reset();
    offsets_reset();
    filelist_reset();

    transport_init();
    sd_begin();

    vib_set_thresholds(settings_vib_hold(), settings_vib_peak(), settings_vib_jerk());
    imu_begin();

    shell_build();
    lv_timer_create([](lv_timer_t*) { shell_tick(); }, 50, nullptr);

    fnc_putchar(0x11);
    fnc_realtime(StatusReport);

    splash_begin();
    lv_timer_handler();
}

// Mirrors loop() in main.cpp. The XModem branches are gone with the SD card
// they read from; everything else is the same sequence.
// Mirrors prime_link_if_up() in main.cpp: the panel asks the controller to
// describe itself exactly once, the first time the link is actually alive.
// Without this the simulator never requests the config dump and every
// capability reads UNKNOWN, which is a different UI from the panel's.
static void prime_link_if_up() {
    static bool primed = false;
    if (primed || state == Disconnected) return;
    primed = true;
    send_line("$#");
    filelist_request();
    fncset_request_all();
    caps_request_config();
}

static void step() {
    sim_transport_tick();
    // Same routing as loop() in main.cpp: while an XModem transfer owns the
    // link, its bytes must go to XModem, never the Grbl parser.
    if (pathload_busy()) {
        for (int i = 0; i < 512; i++) {
            int c = fnc_getchar();
            if (c < 0) break;
            xmodem_rx_feed((uint8_t)c);
        }
        pathload_poll();
    } else {
        for (int i = 0; i < 512; i++) {
            fnc_poll();
            if (!fnc_rx_waiting()) break;
        }
    }
    send_pump();
    prime_link_if_up();
    caps_poll();
    imu_poll();
    service_action();
    lv_timer_handler();
}

// Drive one or more taps through the same indev the mouse uses, pumping the
// loop throughout so LVGL actually samples the press and dispatches the click.
static void run_taps(const char* spec) {
    // Long enough for a scripted walk through a whole tool change; a cut-off
    // script silently skips its last taps and captures too early.
    static char buf[2048];
    if (strlen(spec) >= sizeof(buf)) fprintf(stderr, "[sim] --tap script too long, truncated\n");
    snprintf(buf, sizeof(buf), "%s", spec);

    for (char* tok = strtok(buf, ";"); tok; tok = strtok(nullptr, ";")) {
        // "x,y" taps; "x,y>x2,y2" presses at x,y and drags to x2,y2.
        int x = 0, y = 0, x2 = 0, y2 = 0;
        const int n = sscanf(tok, "%d,%d>%d,%d", &x, &y, &x2, &y2);
        if (n != 2 && n != 4) continue;

        s_px = x;
        s_py = y;
        s_pressed = sim_touch_down = true;
        sim_touch_scans++;
        uint32_t until = millis() + TAP_HOLD_MS;
        while (millis() < until) { step(); SDL_Delay(5); }
        if (n == 4) {
            for (int k = 1; k <= 20; k++) {
                s_px = x + (x2 - x) * k / 20;
                s_py = y + (y2 - y) * k / 20;
                until = millis() + 20;
                while (millis() < until) { step(); SDL_Delay(5); }
            }
        }

        s_pressed = sim_touch_down = false;
        until = millis() + TAP_RELEASE_MS;
        while (millis() < until) { step(); SDL_Delay(5); }

        fprintf(stderr, "[sim] tapped %d,%d\n", x, y);
    }
}

int main(int argc, char** argv) {
    s_epoch_ms = now_ms();

    // A screenshot taken before the splash finishes is a screenshot of the
    // splash. The default clears splash.cpp's timeline (DELAY+SWEEP+HOLD+EXIT,
    // ~4.65s) with margin; --settle overrides it to catch a specific moment.
    uint32_t settle = 6000;

    // Default matches ttybridge.py's own default symlink.
    bool want_devwin = true;
    const char* link = "/tmp/ttyFLUID";
    const char* shot = nullptr;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc)   shot = argv[++i];
        else if (!strcmp(argv[i], "--settle") && i + 1 < argc) settle = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--page") && i + 1 < argc)   s_shot_page = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--link") && i + 1 < argc)   link = argv[++i];
        else if (!strcmp(argv[i], "--no-link"))                link = nullptr;
        else if (!strcmp(argv[i], "--tap") && i + 1 < argc)    s_tap = argv[++i];
        else if (!strcmp(argv[i], "--no-devwin"))               want_devwin = false;
        else if (!strcmp(argv[i], "--gesture"))                 s_gesture = true;
        else if (!strcmp(argv[i], "--alarm") && i + 1 < argc)  s_alarm = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--toolchange") && i + 1 < argc) s_tool = atoi(argv[++i]);
    }

    if (link) sim_link_open(link);
    fprintf(stderr, "[sim] link: %s\n", sim_link_is_live() ? "LIVE FluidNC" : "canned fake");

    // The dummy video driver is enough for a screenshot: nothing below reads
    // pixels back from SDL, only from s_fb.
    if (shot && !getenv("SDL_VIDEODRIVER")) setenv("SDL_VIDEODRIVER", "dummy", 1);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window*   win = SDL_CreateWindow("CNC Console (simulator)",
                                         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                         SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture*  tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          SCREEN_WIDTH, SCREEN_HEIGHT);
    boot();
    if (!shot && want_devwin) devwin_open(SCREEN_WIDTH);

    // Let the splash timeline play out before a screenshot, or every shot is
    // of the splash rather than of the UI you asked to see.
    if (shot) {
        uint32_t until = millis() + settle;
        while (millis() < until) { step(); SDL_Delay(5); }
        if (s_shot_page >= 0 && s_shot_page < PAGE_COUNT) {
            shell_show_page((page_id_t)s_shot_page);
            // Let the page build, tick once and paint before the capture.
            until = millis() + 400;
            while (millis() < until) { step(); SDL_Delay(5); }
        }
        if (s_gesture) {
            sim_touch_pts = 2;
            uint32_t g = millis() + 900;      // > GESTURE_MS
            while (millis() < g) { step(); SDL_Delay(5); }
            sim_touch_pts = 0;
            g = millis() + 300;
            while (millis() < g) { step(); SDL_Delay(5); }
        }
        if (s_alarm) {                  // --alarm N: the fake raises ALARM:N
            sim_raise_alarm(s_alarm);
            uint32_t until = millis() + 600;
            while (millis() < until) { step(); SDL_Delay(5); }
        }
        if (s_tool) {
            sim_tool_change(s_tool);
            uint32_t until = millis() + 600;
            while (millis() < until) { step(); SDL_Delay(5); }
        }
        if (s_tap) run_taps(s_tap);
        const bool ok = write_bmp(shot);
        fprintf(stderr, ok ? "wrote %s\n" : "could not write %s\n", shot);
        SDL_Quit();
        return ok ? 0 : 1;
    }

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_WINDOWEVENT:
                    // Closing the dev window closes only the dev window; the
                    // panel is the application.
                    if (e.window.event == SDL_WINDOWEVENT_CLOSE &&
                        e.window.windowID == devwin_window_id()) devwin_close();
                    break;
                case SDL_MOUSEMOTION: s_px = e.motion.x; s_py = e.motion.y; break;
                case SDL_MOUSEBUTTONDOWN:
                    if (e.button.button == SDL_BUTTON_RIGHT) {
                        // Stands in for a two-finger press: hold the right
                        // button to open or close config mode.
                        sim_touch_pts = 2;
                        break;
                    }
                    s_px = e.button.x; s_py = e.button.y;
                    s_pressed = sim_touch_down = true;
                    s_latched = true;
                    sim_touch_scans++;
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (e.button.button == SDL_BUTTON_RIGHT) { sim_touch_pts = 0; break; }
                    s_pressed = sim_touch_down = false;
                    break;
                case SDL_KEYDOWN:
                    switch (e.key.keysym.sym) {
                        case SDLK_1: sim_set_state("Idle");  break;
                        case SDLK_2: sim_set_state("Run");   break;
                        case SDLK_3: sim_set_state("Hold");  break;
                        case SDLK_4: sim_set_state("Alarm"); break;
                        case SDLK_5: sim_set_state("Jog");   break;
                        case SDLK_6: sim_tool_change(0);     break;
                        case SDLK_SPACE: sim_toggle_motion(); break;
                        case SDLK_s: write_bmp("shot.bmp"); fprintf(stderr, "wrote shot.bmp\n"); break;
                        case SDLK_ESCAPE:
                        case SDLK_q: running = false; break;
                        default: break;
                    }
                    break;
                default: break;
            }
        }

        step();
        devwin_tick();

        SDL_UpdateTexture(tex, NULL, s_fb, SCREEN_WIDTH * sizeof(uint16_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        devwin_present();
        SDL_Delay(5);
    }

    devwin_close();
    SDL_Quit();
    return 0;
}
