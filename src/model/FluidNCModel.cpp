// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "model/console_log.h"
#include "model/caps.h"
#include "FluidNCModel.h"
#include "model/filelist.h"
#include <map>
#include <string>   // arduino-esp32 3.x no longer pulls this in transitively
#include "model/sys_compat.h"
#include "model/model_events.h"
#include "model/model_compat.h"
#include "transport/uart.h"
#include "e4math.h"
#include "model/sendq.h"
#include "model/homed.h"
#include "model/motors.h"
#include "model/linkwatch.h"
#include "model/path_loader.h"
#include "model/offsets.h"
#include "model/fnc_settings.h"
#include <stdarg.h>
#include <stdio.h>


// local copies of status items
const char*        my_state_string    = "N/C";
state_t            state              = Disconnected;  // correct: we are disconnected until FluidNC responds
int                n_axes             = 3;
pos_t              myAxes[6]          = { 0 };
pos_t              myWco[6]           = { 0 };
bool               myLimitSwitches[6] = { false };
bool               myProbeSwitch      = false;
const char*        myFile             = "";  // running SD filename
const char*        myCtrlPins         = "";
file_percent_t     myPercent          = 0.0;  // percent conplete of SD file
override_percent_t myFro              = 100;  // Feed rate override
override_percent_t myRro              = 100;  // Rapid override
override_percent_t mySro              = 100;  // Spindle Override
uint32_t           myFeed             = 0;
uint32_t           mySpeed            = 0;
uint32_t           mySelectedTool     = 0;

std::string myModes = "no data";

int      lastAlarm = 0;
bool     inInches  = false;

// clang-format off
// Maps the state strings in status reports to internal state enum values
struct cmp_str {
   bool operator()(char const *a, char const *b) const    {
      return strcmp(a, b) < 0;
   }
};

std::map<const char *, state_t, cmp_str>  state_map = {
    { "Idle", Idle },
    { "Alarm", Alarm },
    { "Hold:0", Hold },
    { "Hold:1", Hold },
    { "Run", Cycle },
    { "Jog", Jog },
    { "Home", Homing },
    { "Door:0", DoorClosed },
    { "Door:1", DoorOpen },
    { "Check", CheckMode },
    { "Sleep", GrblSleep },
};
// clang-format on

bool decode_state_string(const char* state_string, state_t& state) {
    if (strcmp(my_state_string, state_string) != 0) {
        auto found = state_map.find(state_string);
        if (found != state_map.end()) {
            my_state_string = found->first;
            state           = found->second;
            return true;
        }
    }
    return false;
}

void set_disconnected_state() {
    state           = Disconnected;
    my_state_string = "N/C";
    homed_forget();
}

extern "C" void begin_status_report() {
    myPercent = 0;
}

extern "C" void show_file(const char* filename, file_percent_t percent) {
    myPercent = percent;
}

extern "C" void show_overrides(override_percent_t feed_ovr, override_percent_t rapid_ovr, override_percent_t spindle_ovr) {
    myFro = feed_ovr;
    myRro = rapid_ovr;
    mySro = spindle_ovr;
}

extern "C" void show_feed_spindle(uint32_t feedrate, uint32_t spindle_speed) {
    myFeed  = feedrate;
    mySpeed = spindle_speed;
};

extern "C" void show_limits(bool probe, const bool* limits, size_t n_axis) {
    myProbeSwitch = probe;
    memcpy(myLimitSwitches, limits, n_axis * sizeof(*limits));
}

extern "C" void show_control_pins(const char* pins) {
    //dbg_printf("show_control_pins:%s\r\n", pins);
    myCtrlPins = pins;
}

// ---- stepper-driver health ------------------------------------------------
// Contract settled with the configurator session, 2026-09-07 (see
// /tmp/HANDOFF-driver-io-configurator.md): X/Y1/Y2/Z ride FluidNC macro
// pins, which report in Pn: as digit characters '0'-'3' while active —
// wired active-while-online, so digit present = driver powered and healthy.
// Deliberately NOT alarm-class. A's input is a user_input read via M66
// (poll-only; NOT in Pn:) — that path is not built yet, so A has no badge
// and never gates. The feature arms on the first digit sighting, so a
// machine without the wiring never grows indicators.
static bool s_drv_seen = false;

bool drv_pin(int n) {
    for (const char* p = myCtrlPins; *p; ++p)
        if (*p == '0' + n) return true;
    return false;
}

bool drv_feature_armed() {
    if (!s_drv_seen)
        for (const char* p = myCtrlPins; *p; ++p)
            if (*p >= '0' && *p <= '3') { s_drv_seen = true; break; }
    return s_drv_seen;
}

bool drv_axis_ok(char ax) {
    switch (ax) {
        case 'X': return drv_pin(0);
        case 'Y': return drv_pin(1) && drv_pin(2);   // both gantry drivers
        case 'Z': return drv_pin(3);
        case 'A': return true;   // no telemetry until the M66 path exists
    }
    return true;
}

extern "C" void show_dro(const pos_t* axes, const pos_t* wco, bool isMpos, bool* limits, size_t n_axis) {
    n_axes = (int)n_axis;
    for (int axis = 0; axis < n_axis; axis++) {
        e4_t axis_val = axes[axis];
        if (isMpos) {
            axis_val -= wco[axis];
        }
        myAxes[axis] = inInches ? e4_mm_to_inch(axis_val) : axis_val;
        // Keep the work coordinate offset too. myAxes is the WORK position with
        // wco already removed, so machine position = myAxes + myWco. Without
        // this the DRO can only ever show one of the two.
        myWco[axis] = inInches ? e4_mm_to_inch(wco[axis]) : wco[axis];
    }
}

char axisNumToChar(int axis) {
    return "XYZABC"[axis];
}

const char* mode_string() {
    return myModes.c_str();
}

bool    awaiting_alarm = false;

extern "C" void show_state(const char* state_string) {
    state_t new_state;
    if (decode_state_string(state_string, new_state) && state != new_state) {
        // Leaving Alarm retires its number: the next alarm must be fetched
        // or reported afresh, never shown with the last one's.
        if (state == Alarm) lastAlarm = 0;
        homed_note_state(state == Homing, new_state == Alarm, new_state == Disconnected);
        state = new_state;
        if (state == Alarm && lastAlarm == 0) {  // Unknown
            send_line("$A");                     // Get last alarm
            awaiting_alarm = true;
        }
        act_on_state_change();
    }
}

extern "C" void handle_other(char* line) {
    // Fan out to the offset table, the file list and the console BEFORE the
    // early return below: the file-list reply is exactly the '{' case.
    handle_other_line(line);

    // $-responses are config and {...} lines are JSON replies; both were
    // already offered to their consumers by handle_other_line() above.
    if (*line == '$' || *line == '{') return;
    int alarmlen = strlen("Active alarm: ");
    if (strncmp(line, "Active alarm: ", alarmlen) == 0) {
        lastAlarm = atoi(line + alarmlen);
        homed_note_alarm(lastAlarm);
        if (awaiting_alarm) {
            dbg_printf("Got alarm %d\n", lastAlarm);
            awaiting_alarm = false;
            act_on_state_change();
        }
    }
}

extern "C" void show_error(int error) {
    {
        char b[16];
        snprintf(b, sizeof(b), "error:%d", error);
        clog_add(b);   // named by console_log
    }
    caps_reply_end();
    cmd_error(error);
    request_redisplay();
}

// Only fnc_send_line() raises this, and nothing calls it: send_now() keeps
// the ack gate here. Kept so the parser's weak default is never what runs.
extern "C" void show_timeout() {}
// The parser consumes ok / error:N / ALARM:N itself, so they never reached the
// console log as plain lines -- the console showed no replies at all, and the
// FluidNC names console_log gives error and alarm codes never appeared.
// [MSG:...] lines. GrblParserC's default handle_msg() is an empty weak stub,
// and nothing here overrode it, so every FluidNC message -- INFO, WARN, ERR,
// the $CI channel list, the Airedale's "BOARD:" banner -- was dropped unseen.
extern "C" void handle_msg(char* command, char* arguments) {
    model_flag_message(command, arguments);
}

extern "C" void show_ok() {
    clog_add("ok");
    caps_reply_end();
    cmd_ok();
}

extern "C" void end_status_report() {
    model_flag_dro();
}

extern "C" void show_alarm(int alarm) {
    {
        char b[16];
        snprintf(b, sizeof(b), "ALARM:%d", alarm);
        clog_add(b);
    }
    lastAlarm = alarm;
    homed_note_alarm(alarm);
    cmd_alarm(alarm);
    request_redisplay();
}

extern "C" void show_gcode_modes(struct gcode_modes* modes) {
    inInches = strcmp(modes->units, "In") == 0 || strcmp(modes->units, "G20") == 0;

    myModes = modes->wcs;
    myModes += " ";
    myModes += modes->units;
    myModes += " ";
    myModes += modes->distance;
    myModes += " ";
    myModes += modes->spindle;
    if (strcmp(modes->mist, "On") == 0) {
        myModes += " Mist";
    }
    if (strcmp(modes->flood, "On") == 0) {
        myModes += " Flood";
    }

    mySelectedTool = modes->tool;
    request_redisplay();
}
