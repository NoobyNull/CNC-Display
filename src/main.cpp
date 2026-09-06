#include <Arduino.h>
#include <Wire.h>
#include <esp_system.h>

// There is no native-USB console on the Advance 7.0: the Type-C is a CH340K on
// UART0 (GPIO 43/44) — the SAME pins the UART0-IN/OUT connectors expose, which
// is where the FluidNC link plugs in. One UART, two masters, so the Setup
// "Wired UART" toggle decides who owns it at boot: toggle ON = the link runs
// at 1 Mbaud and the console stays silent; toggle OFF = a 115200 debug console
// on the USB Type-C and the link is dead. Flashing is the ROM bootloader over
// the CH340 and works either way (unplug the link cable if esptool misbehaves).
#include "hw/display_hw.h"
#include "hw/stc8.h"
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
#include "model/fnc_settings.h"
#include "model/caps.h"
#include "model/filelist.h"
#include "model/path_loader.h"
#include "ui/shell.h"
#include "ui/splash.h"
#include "GrblParserC.h"

// Backlight level (0..255) known to survive on the weakest supply the panel
// sees in practice — the 5V pin of the UART link cable. ~50%.
static const uint8_t BL_BROWNOUT_SAFE = 128;

static void ui_tick_cb(lv_timer_t*) { shell_tick(); }

// Touch is polled on its own timer so the GT911 I2C transaction is not tied to
// the UART drain rate.
static void touch_tick_cb(lv_timer_t*) { touch_poll(); }

void setup() {
    settings_load();
    // Console and FluidNC link share UART0's pins — see the header comment.
    // Un-begun, every Serial.print is a safe no-op.
    if (!settings_uart_enabled()) Serial.begin(115200);
    Serial.println("\n=== CNC Console ===");

    // The GT911 and the STC8 share one bus, and the STC8 owns the touch reset
    // line. Nothing below may touch Wire before this line.
    Wire.begin(GT911_SDA_PIN, GT911_SCL_PIN);
    Wire.setClock(400000);

    touch_init();          // create the I2C mutex before anything uses the bus
    stc8_init();           // wait for the housekeeping MCU to answer
    gt911_reset();         // address strap is latched here, before any probing
    display_init();        // RGB panel
    gt911_discover();
    gt911_dump_config();   // read-only; the stall lead lives at 0x804D and 0x8055
    lvgl_init();
    // A high saved brightness can sag a thin 5V feed (UART-connector power)
    // below the brownout threshold the instant it's restored, and the panel
    // boot-loops forever. If the last reset WAS a brownout, come up dim for
    // this boot instead of re-slamming the saved level; NVS is untouched, so
    // a healthy supply gets the user's setting back on the next power cycle.
    uint8_t bl = settings_brightness();
    if (esp_reset_reason() == ESP_RST_BROWNOUT && bl > BL_BROWNOUT_SAFE) {
        Serial.printf("[power] brownout reset — backlight %u -> %u for this boot\n",
                      bl, BL_BROWNOUT_SAFE);
        bl = BL_BROWNOUT_SAFE;
    }
    set_backlight(bl);

    // Splash first, painted now: the rest of boot (SD probe, IMU, shell build)
    // happens behind it instead of behind a black screen.
    splash_show();
    lv_timer_handler();

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
    lv_timer_create(touch_tick_cb, 10, nullptr);   // 100 Hz gate; the read itself is INT-driven

    Serial.printf("PSRAM free: %u\n", (unsigned)ESP.getFreePsram());
    Serial.printf("Heap free:  %u\n", (unsigned)ESP.getFreeHeap());

    // Nothing here may wait on the controller. fnc_send_line() spins in
    // `while (_ackwait)` until the previous line is acked or its timeout
    // (2000ms) expires, so chaining sends at boot with no controller attached
    // freezes the panel for seconds before the first frame is even painted.
    // Only these two are safe: both are a single putchar with no ack expected.
    fnc_putchar(0x11);          // XON, clear any stale XOFF on the far end
    fnc_realtime(StatusReport);

    // Boot is done: start the splash timeline now, so the whole sweep plays on
    // screen instead of being eaten by the blocking init above.
    splash_begin();

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

// transport_poll() keeps poking the controller until FluidNC answers.
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
    Serial.printf("[touch] down=%d scans=%lu polls=%lu lockfail=%lu staterr=%lu int=%u stat=0x%02X\n",
                  touch_is_down() ? 1 : 0,
                  (unsigned long)touch_fresh_count(),
                  (unsigned long)touch_poll_count(),
                  (unsigned long)touch_lock_fails(),
                  (unsigned long)touch_stat_errs(),
                  touch_int_level(), touch_last_status());
}

static void prime_link_if_up() {
    if (s_primed) return;
    if (state == Disconnected) return;
    s_primed = true;
    // Config dump first: it decides which controls are even offered, and every
    // second it is late is a second the panel shows homing and probing to a
    // machine that has neither.
    caps_request_config();
    send_line("$#");
    filelist_request();
    // The full settings dump also feeds the Diag limit lamps: which lamps
    // exist is read from the controller's configured limit/probe pins.
    fncset_request_all();
    Serial.println("link up: primed config, offsets, file list and settings");
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
        // 512 matches the XModem paths: at 64 a $S dump outran the drain and
        // overflowed the RX buffer.
        for (int i = 0; i < 512; i++) {
            fnc_poll();
            if (!fnc_rx_waiting()) break;
        }
    }
    send_pump();   // queued controller lines; never sent from a UI callback
    imu_poll();
    vib_guard();
    upload_poll();
    prime_link_if_up();
    caps_poll();
    link_watchdog();
    service_action();
    lv_timer_handler();
}
