// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#include "GrblParserC.h"

// Same states as FluidNC except for the last one
enum state_t {
    Idle = 0,   // Must be zero.
    Alarm,      // In alarm state. Locks out all g-code processes. Allows settings access.
    CheckMode,  // G-code check mode. Locks out planner and motion only.
    Homing,     // Performing homing cycle
    Cycle,      // Cycle is running or motions are being executed.
    Hold,       // Active feed hold
    Jog,        // Jogging mode.
    DoorOpen,
    DoorClosed,
    GrblSleep,     // Sleep state.
    ConfigAlarm,   // You can't do anything but fix your config file.
    Critical,      // You can't do anything but reset with CTRL-x or the reset button
    Disconnected,  // We can't talk to FluidNC
};

// Variables and functions to model the state of the FluidNC controller

extern state_t     state;
extern const char* my_state_string;

extern int                n_axes;
extern pos_t              myAxes[6];   // WORK position (wco already subtracted)
extern pos_t              myWco[6];    // work coordinate offset; machine = myAxes + myWco
extern bool               myLimitSwitches[6];
extern bool               myProbeSwitch;
extern const char*        myCtrlPins;

// Stepper-driver health, carried in Pn: as digits (0=X 1=Y1 2=Y2 3=Z 4=A,
// present while the driver is online). Armed on first sighting.
bool drv_pin(int n);
bool drv_feature_armed();
bool drv_axis_ok(char axis);
void drv_reset_axis(char axis);   // pulse its reset relay(s); no-op for A
extern const char*        myFile;
extern file_percent_t     myPercent;
extern override_percent_t myFro;
extern override_percent_t myRro;   // rapid override; FluidDial discarded this
extern override_percent_t mySro;
extern uint32_t           myFeed;
extern uint32_t           mySpeed;
extern int                lastAlarm;
extern bool               inInches;
extern uint32_t           mySelectedTool;

void send_line(const char* s, int timeout = 2000);
void send_jog_line(const char* s);  // one jog chunk; skipped if the controller is behind

// send_line() queues rather than blocking: fnc_send_line() waits for the
// previous ack inside lv_timer_handler(), which freezes touch. Drain from
// loop(). See the comment block in FluidNCModel.cpp.
void send_pump();
bool send_busy();   // an ack is still outstanding
void send_linef(const char* fmt, ...);

// Multi-line actions that only make sense whole (AutoZero, a relative move and
// its G90, a driver reset). Stage with seq_line(), then seq_send(): the lines
// are queued all together or not at all, and false means refused, with the
// reason already on screen. If the controller answers any line with an error,
// or alarms, the rest are not sent and the cleanup lines (optional, e.g. "G90";
// several are separated by '\n') go instead.
void seq_begin(const char* name);
void seq_cleanup(const char* line);
void seq_line(const char* s);
void seq_linef(const char* fmt, ...);
bool seq_send();

// Drop everything queued. Lines already written are not recalled.
void send_flush(const char* why);
// Soft-reset the controller (Ctrl-X) and drop everything queued, so nothing
// waiting behind the reset runs after it.
void send_reset();
bool send_reset_was_recent();

// Link lifecycle, once per loop(): liveness (NO LINK after 4 s of silence),
// controller restarts, and priming a fresh connection.
void link_service();

void send_jog_cancel();

char        axisNumToChar(int axis);
const char* mode_string();

void set_disconnected_state();
