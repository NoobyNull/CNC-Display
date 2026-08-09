#include <Arduino.h>
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

    touch_init();          // create the I2C mutex before anything touches the bus
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

    fnc_putchar(0x11);          // XON, clear any stale XOFF on the far end
    fnc_realtime(StatusReport);
    send_line("$#");            // prime the work offset table
    filelist_request();
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
    service_action();
    lv_timer_handler();
}
