#include "link/link.h"
#include "model/calibration.h"
#include "cmd/cmd.h"
#include "model/FluidNCModel.h"
#include "model/caps.h"
#include "model/console_log.h"
#include "model/filelist.h"
#include "model/fnc_settings.h"
#include "model/homed.h"
#include "model/linkwatch.h"
#include "model/model_compat.h"
#include "model/model_events.h"
#include "model/motors.h"
#include "model/offsets.h"
#include "model/path_loader.h"
#include "transport/uart.h"
#include "GrblParserC.h"

// One place decides when the link is up, down, or talking to a controller that
// has just restarted, and what the panel does about each.

static bool s_primed = false;

// Everything the panel asks the controller once per connection. A controller
// restart wipes all of it (auto-report, parser modes, the panel's view of the
// config), so it runs again after one.
static void prime() {
    s_primed = true;
    bootlog_printf("link up (%s): priming", my_state_string);
    resetFlowControl();                   // clear any stale XOFF on the link
    fnc_realtime((realtime_cmd_t)0x0c);   // Ctrl-L: echo off
    send_line("$G");                      // G-code modes
    send_line("$RI=200");                 // auto-report every 200 ms
    motors_resync();                      // a drop may have been the controller rebooting
    motors_on_link();
    // Config dump first: it decides which controls are even offered.
    caps_request_config();
    send_line("$#");
    filelist_request();
    // The full settings dump also feeds the Diag limit lamps.
    fncset_request_all();
    // Steps per mm is a config item, not in the $S dump. Ask for it now so the
    // Cal page shows the controller's real values without a READ press.
    cal_reset();
    send_line("$/axes/x/steps_per_mm");
    send_line("$/axes/y/steps_per_mm");
    send_line("$/axes/z/steps_per_mm");
}

static void link_lost() {
    clog_add("[MSG:WARN: link lost - no reply from the controller for 4 s]");
    send_flush("link lost");
    set_disconnected_state();
    s_primed = false;
    act_on_state_change();
}

void link_service() {
    // While XModem owns the wire a '?' would land in the middle of the
    // transfer, so the watch stands down (and starts a fresh window after).
    const bool watch = state != Disconnected && !pathload_busy();
    switch (linkwatch_tick(milliseconds(), fnc_rx_count(), watch)) {
        case LINKWATCH_PING: fnc_realtime(StatusReport); break;
        case LINKWATCH_LOST: link_lost(); break;
        default: break;
    }

    if (model_take_board_reset()) {
        // The banner follows a soft reset too. One the panel sent itself keeps
        // position (FluidNC alarms if it did not); anything else may have been
        // a power cycle, so homing is no longer trusted.
        const bool ours = send_reset_was_recent();
        send_flush(ours ? nullptr : "controller restarted");
        if (!ours) {
            clog_add("[MSG:WARN: controller restarted - home before moving]");
            homed_forget();
        }
        s_primed = false;
    }
    if (state == Disconnected) { s_primed = false; return; }
    if (!s_primed) prime();
}

