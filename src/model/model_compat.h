#pragma once

// Redraw and boot-log plumbing between the controller callbacks and the UI.

// Coalesced redraw request from parser callbacks.
void request_redisplay();
bool take_redisplay();

// A state transition the UI should react to.
void act_on_state_change();

// Boot-time diagnostics: to the debug port and the console view.
void bootlog_printf(const char* fmt, ...);

// Implemented in transport/uart.cpp: sends XON to clear a stale XOFF.
void resetFlowControl();
