#pragma once

// Shims for the FluidDial plumbing that FluidNCModel.cpp calls into. Each one
// is either a real reimplementation against this project's own machinery or an
// honest no-op with the reason stated. Nothing here is a placeholder.

typedef void (*ActionHandler)(void);

// Deferred action, run from the main loop where the parser is idle. FluidDial
// needed this because acting on a state change from inside a parser callback
// corrupts the report buffer; the same is true here.
void schedule_action(ActionHandler action);
void service_action();

// Coalesced redraw request from parser callbacks.
void request_redisplay();
bool take_redisplay();

// A state transition the UI should react to.
void act_on_state_change();

// Boot-time diagnostics. FluidDial buffers these for an on-screen boot log;
// here they go to the debug port and the console view.
void bootlog_printf(const char* fmt, ...);

// --- deliberate no-ops, with reasons ---------------------------------------

// This project does not use the streaming JSON parser (filelist.cpp scans the
// fixed reply shape directly), so there is never an in-flight document to
// abandon or a brace depth to reset.
bool json_in_progress();
void json_reset_depth();

// FluidDial advances a macro-file request chain when a bare "error:N" arrives.
// There is no macro chain here — macros are fired one at a time by $RM.
extern "C" void file_request_failed_advance();

// Per-axis homed state is not displayed, so there is nothing to probe for.
void detect_homing_info();

// $-responses are ordinary lines here and already fan out through
// handle_other_line() before this is reached.
void parse_dollar(char* line);

// Implemented in transport/uart.cpp: sends XON to clear a stale XOFF.
void resetFlowControl();
