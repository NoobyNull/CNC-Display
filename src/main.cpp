#include <Arduino.h>
#include <Wire.h>

// The console is USB CDC on the native USB-Serial-JTAG peripheral (GPIO 19/20,
// the Type-C marked USB), set up by ARDUINO_USB_CDC_ON_BOOT in platformio.ini.
// It costs no pins the panel or the link wants, and it is also the flashing
// port, so the FluidNC link on 43/44 never has to be given up to update the
// board. That is what the old UPDATE/PENDANT mode existed to work around.
#include "hw/display_hw.h"
#include "hw/io_expander.h"
#include "hw/touch.h"
#include "hw/sdcard.h"
#include "hw/imu.h"
#include "model/vibration.h"
#include "model/file_upload.h"
#include "transport/uart.h"
#include "transport/transport.h"
#include "model/settings.h"
#include "model/screensaver.h"
#include "transport/xmodem_rx.h"
#include "transport/xmodem_tx.h"
#include "model/FluidNCModel.h"
#include "model/model_events.h"
#include "model/model_compat.h"
#include "model/console_log.h"
#include "model/offsets.h"
#include "model/filelist.h"
#include "model/path_loader.h"
#include "ui/shell.h"
#include "GrblParserC.h"

static void ui_tick_cb(lv_timer_t*) { shell_tick(); }

// Touch is polled on its own timer so the GT911 I2C transaction is not tied to
// the UART drain rate.
static void touch_tick_cb(lv_timer_t*) { touch_poll(); }

void setup() {
    settings_load();
    Serial.begin(115200);
    // A host that stops draining the CDC must not stall the UI. Without this,
    // every Serial.printf blocks up to 100ms waiting for the ring buffer.
    Serial.setTxTimeoutMs(0);
    Serial.println("\n=== CNC Console ===");

    // The GT911 and the io-extender share one bus, and the extender gates the
    // panel rails and the touch reset. Nothing below may touch Wire before
    // this line.
    Wire.begin(GT911_SDA_PIN, GT911_SCL_PIN);
    Wire.setClock(400000);

    touch_init();          // create the I2C mutex before anything uses the bus
    ioexp_init();          // panel rails, touch reset line, SD DAT3, USB/CAN mux
    gt911_reset();         // address strap is latched here, before any probing
    display_init();        // LCD rails + RGB panel
    gt911_discover();
    lvgl_init();
    set_backlight(settings_brightness());

    clog_reset();
    offsets_reset();
    filelist_reset();

    transport_init();
    sd_begin();

    // Apply the saved sensitivity before the IMU starts feeding samples, or the
    // first seconds of a session run on the compiled defaults instead of the
    // tuning this machine was actually given.
    vib_set_thresholds(settings_vib_hold(), settings_vib_peak(), settings_vib_jerk());
    imu_begin();

    shell_build();
    lv_timer_create(ui_tick_cb, 50, nullptr);      // 20 Hz UI
    lv_timer_create(touch_tick_cb, 20, nullptr);   // 50 Hz touch

    Serial.printf("PSRAM free: %u\n", (unsigned)ESP.getFreePsram());
    Serial.printf("Heap free:  %u\n", (unsigned)ESP.getFreeHeap());

    // Nothing here may wait on the controller. fnc_send_line() spins in
    // `while (_ackwait)` until the previous line is acked or its timeout
    // (2000ms) expires, so chaining sends at boot with no controller attached
    // freezes the panel for seconds before the first frame is even painted.
    // Only these two are safe: both are a single putchar with no ack expected.
    fnc_putchar(0x11);          // XON, clear any stale XOFF on the far end
    fnc_realtime(StatusReport);

    // Paint frame one now so the UI is up and interactive regardless of whether
    // anything is on the other end of the wire.
    lv_timer_handler();
}

// The work offsets and file list are primed once, the first time the link is
// actually alive. Doing it at boot would either block (see above) or be sent
// into the void and never retried.
static bool s_primed = false;

// Act on the vibration analyser according to the Setup mode. Deliberately
// conservative:
//   monitor - never touches the machine
//   soft    - feed hold on either condition
//   hard    - feed hold on sustained vibration, soft reset on impact
//
// This is advisory, not a safety interlock. It rides the same link as
// everything else, so a dead link means no protection. It does not replace a
// hardware e-stop wired to the controller.
static void vib_guard() {
    static uint32_t rearm_ms = 0;
    static bool     tripped  = false;

    uint8_t mode = settings_vib_mode();
    if (mode == 0 || !imu_present()) { tripped = false; return; }

    vib_level_t lvl = vib_result()->level;

    // Only act while the machine is actually moving. Holding an idle machine is
    // noise, and jogging the panel around on the bench should not stop a job.
    bool moving = (state == Cycle || state == Jog || state == Homing);
    if (!moving || lvl == VIB_NONE) {
        if (lvl == VIB_NONE) tripped = false;
        return;
    }

    // One action per event: without the latch a 200Hz analyser would send a
    // hold every 5ms for as long as the vibration lasts.
    uint32_t now = millis();
    if (tripped && (int32_t)(now - rearm_ms) < 0) return;
    tripped  = true;
    rearm_ms = now + 3000;

    if (lvl == VIB_ESTOP && mode == 2) {
        fnc_realtime(Reset);
        clog_add("[MSG:VIBRATION: impact - soft reset sent]");
    } else {
        fnc_realtime(FeedHold);
        clog_add(lvl == VIB_ESTOP ? "[MSG:VIBRATION: impact - feed hold sent]"
                                  : "[MSG:VIBRATION: sustained - feed hold sent]");
    }
}

// Link hunting lives in transport.cpp, which cycles the two candidate UART baud
// rates until FluidNC answers.
static void link_watchdog() {
    static uint32_t next_log = 0;
    uint32_t now = millis();
    transport_poll();
    if ((int32_t)(now - next_log) < 0) return;
    next_log = now + 2000;
    Serial.printf("[link] %-10s %-32s state=%d rx=%lu tx=%lu  scroff=%umin idle=%lus%s\n",
                  transport_label(), transport_detail(),
                  (int)state, fnc_rx_count(), fnc_tx_count(),
                  (unsigned)settings_screen_off_min(),
                  (unsigned long)(lv_disp_get_inactive_time(NULL) / 1000),
                  screensaver_is_off() ? " BLANK" : "");
    Serial.printf("[touch] down=%d scans=%lu polls=%lu lockfail=%lu staterr=%lu resets=%lu\n",
                  touch_is_down() ? 1 : 0,
                  (unsigned long)touch_fresh_count(),
                  (unsigned long)touch_poll_count(),
                  (unsigned long)touch_lock_fails(),
                  (unsigned long)touch_stat_errs(),
                  (unsigned long)touch_reset_count());
}

static void prime_link_if_up() {
    if (s_primed) return;
    if (state == Disconnected) return;
    s_primed = true;
    send_line("$#");
    filelist_request();
    Serial.println("link up: primed offsets and file list");
}

void loop() {
    if (upload_busy()) {
        // XModem owns the link while a file is going out; the Grbl parser must
        // not see ACK/NAK bytes.
        for (int i = 0; i < 512; i++) {
            int c = fnc_getchar();
            if (c < 0) break;
            xmodem_tx_feed((uint8_t)c);
        }
    } else if (pathload_busy()) {
        // XModem owns the link during a transfer: the Grbl parser must never see
        // block headers, sequence bytes or CRCs.
        for (int i = 0; i < 512; i++) {
            int c = fnc_getchar();
            if (c < 0) break;
            xmodem_rx_feed((uint8_t)c);
        }
        pathload_poll();
    } else {
        // fnc_poll() drains one byte. The link delivers far more per tick than
        // that, so drain a chunk and stop as soon as RX is empty — draining
        // blindly adds idle-loop jitter that makes small jog moves choppy.
        for (int i = 0; i < 64; i++) {
            fnc_poll();
            if (!fnc_rx_waiting()) break;
        }
    }
    imu_poll();
    vib_guard();
    upload_poll();
    prime_link_if_up();
    link_watchdog();
    service_action();
    lv_timer_handler();
}
