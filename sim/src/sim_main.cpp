// Simulator entry point: the same boot order and the same loop body as
// src/main.cpp, minus the parts that only exist on the panel (Wire, the STC8
// bring-up sequence, the GT911 probe, PSRAM reporting).
//
// This file is the one place the simulator duplicates firmware logic rather
// than reusing it, because setup()/loop() are Arduino's shape. Keep the loop
// body below in step with src/main.cpp's, or the simulator stops predicting
// the panel's behaviour.

#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Arduino.h>

#include "hw/display_hw.h"
#include "hw/touch.h"
#include "hw/sdcard.h"
#include "hw/imu.h"
#include "model/settings.h"
#include "model/screensaver.h"
#include "model/vibration.h"
#include "model/file_upload.h"
#include "model/FluidNCModel.h"
#include "model/model_compat.h"
#include "model/console_log.h"
#include "model/offsets.h"
#include "model/fnc_settings.h"
#include "model/caps.h"
#include "model/filelist.h"
#include "model/path_loader.h"
#include "transport/transport.h"
#include "transport/uart.h"
#include "transport/xmodem_rx.h"
#include "transport/xmodem_tx.h"
#include "ui/shell.h"
#include "ui/splash.h"
#include "GrblParserC.h"

SimSerial Serial;

extern bool sim_should_quit;
void sim_link_target(const char* host, int port);
void sd_set_root(const char* root);
bool sim_screenshot(const char* path);

// Page names for --page, in page_id_t order.
static const char* PAGE_NAMES[PAGE_COUNT] = {
    "jog", "home", "probe", "offsets", "run", "macro", "motion", "cal", "diag", "setup"
};

static void ui_tick_cb(lv_timer_t*) { shell_tick(); }
static void touch_tick_cb(lv_timer_t*) { touch_poll(); }

static bool s_primed = false;

static void prime_link_if_up() {
    if (s_primed) return;
    if (state == Disconnected) return;
    s_primed = true;
    caps_request_config();
    send_line("$#");
    filelist_request();
    fncset_request_all();
    Serial.println("link up: primed config, offsets, file list and settings");
}

static void link_watchdog() {
    static uint32_t next_log = 0;
    uint32_t now = millis();
    transport_poll();
    if ((int32_t)(now - next_log) < 0) return;
    next_log = now + 2000;
    Serial.printf("[link] %-8s %-40s state=%d rx=%lu tx=%lu\n",
                  transport_label(), transport_detail(),
                  (int)state, fnc_rx_count(), fnc_tx_count());
}

int main(int argc, char** argv) {
    const char* host = "127.0.0.1";
    int         port = 2323;
    const char* root = "sim/sdcard";
    const char* page = nullptr;
    const char* shot = nullptr;
    uint32_t    shot_ms = 3000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc)      host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sd") && i + 1 < argc)   root = argv[++i];
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) page = argv[++i];
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--shot-ms") && i + 1 < argc) shot_ms = (uint32_t)atoi(argv[++i]);
        else {
            printf("usage: %s [--host H] [--port P] [--sd DIR]\n"
                   "          [--page NAME] [--shot FILE.bmp] [--shot-ms N]\n"
                   "pages: jog home probe offsets run macro motion cal diag setup\n", argv[0]);
            return argv[i][0] == '-' ? 1 : 0;
        }
    }
    sim_link_target(host, port);
    sd_set_root(root);

    settings_load();
    Serial.println("\n=== CNC Console (simulator) ===");

    display_init();
    lvgl_init();
    set_backlight(settings_brightness());

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
    lv_timer_create(ui_tick_cb, 50, nullptr);
    lv_timer_create(touch_tick_cb, 10, nullptr);

    fnc_putchar(0x11);            // XON, as the panel does at boot
    fnc_realtime(StatusReport);

    splash_begin();
    lv_timer_handler();

    if (page) {
        int id = -1;
        for (int i = 0; i < PAGE_COUNT; i++)
            if (!strcmp(page, PAGE_NAMES[i])) id = i;
        if (id < 0) { fprintf(stderr, "unknown page '%s'\n", page); return 1; }
        shell_show_page((page_id_t)id);
    }

    const uint32_t t0 = millis();
    bool shot_done = false;

    while (!sim_should_quit) {
        if (upload_busy()) {
            for (int i = 0; i < 512; i++) {
                int c = fnc_getchar();
                if (c < 0) break;
                xmodem_tx_feed((uint8_t)c);
            }
        } else if (pathload_busy()) {
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
        imu_poll();
        upload_poll();
        prime_link_if_up();
        link_watchdog();
        service_action();
        lv_timer_handler();

        // Screenshot mode: give the link time to prime the page with real
        // controller data, grab the frame, then exit with a status that says
        // whether the grab worked.
        if (shot && !shot_done && (millis() - t0) >= shot_ms) {
            shot_done = true;
            bool ok = sim_screenshot(shot);
            Serial.printf("screenshot %s -> %s\n", ok ? "saved" : "FAILED", shot);
            return ok ? 0 : 1;
        }

        // The panel's loop is paced by real work on a single core; here it
        // would spin a desktop core flat out for no extra fidelity.
        usleep(2000);
    }

    Serial.println("simulator exiting");
    return 0;
}
