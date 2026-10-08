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
#include "hw/status_led.h"
#include "hw/fw_update.h"
#include "esp32-hal-periman.h"
#include "model/vibration.h"
#include "model/imu_log.h"
#include "transport/uart.h"
#include "transport/transport.h"
#include "model/settings.h"
#include "model/screensaver.h"
#include "transport/xmodem_rx.h"
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
    // FORCE_CONSOLE (build flag): boot log on regardless, for a panel whose
    // Setup page cannot be reached. The link reopens these pins later, so it
    // With FORCE_CONSOLE the link is not started at all (transport.cpp), so the
    // console stays readable for the whole run.
#ifdef FORCE_CONSOLE
    Serial.begin(115200);
#else
    if (!settings_uart_enabled()) Serial.begin(115200);
#endif
    Serial.println("\n=== CNC Console ===");

    // The GT911 and the STC8 share one bus, and the STC8 owns the touch reset
    // line. Nothing below may touch Wire before this line.
    Wire.begin(GT911_SDA_PIN, GT911_SCL_PIN);
    Wire.setClock(I2C_HZ);

    touch_init();          // create the I2C mutex before anything uses the bus
    stc8_init();           // wait for the housekeeping MCU to answer
    // Probe first, reset only on a miss, before the display comes up -- the
    // factory's order. A GT911 that is already up is not reset needlessly.
    gt911_discover();
    display_init();        // RGB panel
#ifdef FORCE_CONSOLE
    // Who answers on the bus: GT911 0x5D/0x14, STC8 0x30, RTC 0x51, IMU 0x68/69.
    Serial.print("[i2c] scan:");
    for (uint8_t a = 0x08; a < 0x78; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
    }
    Serial.println();
#endif
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
    status_led_begin();
    // Where a dark status bar is diagnosed: the pin must belong to RMT_TX.
    // SPI_MASTER_MISO means the SD card still holds it (DIP on MIC & TF).
    bootlog_printf("SD %s; status bar IO%d owned by %s", sd_present() ? "mounted" : "absent",
                   status_led_pin(), perimanGetTypeName(perimanGetPinBusType(status_led_pin())));

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

// Single-character commands on the USB console. Only reachable when the Setup
// "Wired UART" toggle is OFF, since that is the one configuration where Serial
// is begun at all (see the header comment at the top of this file) — with the
// link up this is dead code and the REC button on the Diag page is the only
// control there is.
//
// This exists because the console and the link are the same two pins but they
// are NOT the same cable at the host end: FluidNC is a CP2102 and this panel is
// a CH340K, so a PC can hold both at once. Console mode gives up the panel's
// own view of the machine and hands the correlation to the PC, which is driving
// the G-code anyway.
static void console_cmd() {
    if (settings_uart_enabled()) return;
    while (Serial.available()) {
        switch (Serial.read()) {
        case 'r':
            if (!imulog_start()) Serial.printf("# start failed: %s\r\n", imulog_status());
            break;
        case 's':
            imulog_stop();
            imulog_set_tty(false);
            break;
        case 't':
            imulog_set_tty(!imulog_tty());
            if (!imulog_tty()) Serial.println("# stream off");
            break;
        case '?':
            Serial.printf("# %s  file=%s  rate=%uHz  samples=%lu dropped=%lu skips=%lu imu=%s\r\n",
                          imulog_status(), imulog_path()[0] ? imulog_path() : "-",
                          (unsigned)imu_rate(),
                          (unsigned long)imulog_count(), (unsigned long)imulog_dropped(),
                          (unsigned long)imu_skips(),
                          imu_present() ? "yes" : "NOT FOUND");
            break;
        case 'h':
            Serial.println("# r=record to SD  s=stop  t=toggle console stream  ?=status");
            break;
        default:
            break;    // newlines and stray bytes from a terminal
        }
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
    // A controller reset wipes everything primed below, plus the $RI auto-report
    // and the parser modes, and the link may never drop to Disconnected if the
    // reboot is quick. Left alone the jog page falls back to its 500 mm/min
    // creep rate because the axis max rates are gone.
    bool reset = model_take_board_reset();
    if (state == Disconnected) { s_primed = false; return; }
    if (reset && s_primed) {
        s_primed = false;
        send_line("$G");
        send_line("$RI=200");
        Serial.println("controller reset seen: re-priming");
    }
    if (s_primed) return;
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

// [MSG:PANEL:UPDATE] from the controller runs the same network update as the
// Setup button. Never mid-job: the update blanks the screen and reboots. The
// update's own tap-to-cancel countdown still gives whoever is at the panel the
// last word.
static void remote_update() {
    if (!model_take_update_request()) return;
    if (!(state == Idle || state == Alarm)) {
        clog_add("[MSG:WARN: remote update refused - machine not Idle]");
        return;
    }
    clog_add("[MSG:INFO: remote update requested]");
    fwupd_net();   // only returns on failure or cancel
}

void loop() {
    if (pathload_busy()) {
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
    if (uart_poll_events()) {
        // Bytes lost below the parser: say so in the console, where a shredded
        // settings line or reply would otherwise look like a parsing bug.
        const uart_rx_losses_t& l = uart_rx_losses();
        char b[96];
        snprintf(b, sizeof(b), "[MSG:WARN: UART RX lost bytes: fifo_ovf=%lu buf_full=%lu frame_err=%lu parity=%lu]",
                 l.fifo_ovf, l.buf_full, l.frame_err, l.parity_err);
        clog_add(b);
    }
    send_pump();   // queued controller lines; never sent from a UI callback
    imu_poll();
    status_led_tick();
    // Straight after imu_poll(), so a sample taken this pass reaches the card on
    // the same pass. The FluidNC drain at the top of the loop is therefore the
    // very next thing to run: an SD write that stalls is followed immediately by
    // a read, which is what keeps the 16K RX ring (~160 ms at 1 Mbaud) clear.
    imulog_poll();
    console_cmd();
    vib_guard();
    prime_link_if_up();
    caps_poll();
    link_watchdog();
    service_action();
    remote_update();
    lv_timer_handler();
}
