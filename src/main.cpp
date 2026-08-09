#include <Arduino.h>
#include <Wire.h>
#include "hw/display_hw.h"
#include "hw/touch.h"
#include "transport/uart.h"
#include "transport/xmodem_rx.h"
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
    Serial.begin(115200);
    Serial.println("\n=== CNC Console ===");

    // The GT911 and the PCA9557 share one bus. Nothing below may touch Wire
    // before this line — display_init() drives the PCA9557 immediately.
    Wire.begin(19, 20);    // SDA=19, SCL=20

    touch_init();          // create the I2C mutex before anything uses the bus
    display_init();        // PCA9557 reset + RGB panel
    gt911_discover();      // must follow display_init(): it resets the GT911
    lvgl_init();
    set_backlight(200);

    clog_reset();
    offsets_reset();
    filelist_reset();

    init_fnc_uart();

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

static void prime_link_if_up() {
    if (s_primed) return;
    if (state == Disconnected) return;
    s_primed = true;
    send_line("$#");
    filelist_request();
    Serial.println("link up: primed offsets and file list");
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
        for (int i = 0; i < 64; i++) {
            fnc_poll();
            if (!fnc_rx_waiting()) break;
        }
    }
    prime_link_if_up();
    service_action();
    lv_timer_handler();
}
