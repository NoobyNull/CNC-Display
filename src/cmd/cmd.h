#pragma once
#include <stdint.h>

// The command layer: everything the panel says to the controller goes through
// here. Nothing in the UI writes to the link directly.
//
// Lines are queued and drained from loop() one at a time, each waiting for its
// ok or error, so a slow controller costs latency, never a frozen screen.
// Multi-line actions are sequences: queued whole or refused, stopped at the
// first error or alarm, never left half-sent.

// A plain line. Queued; sent as soon as the previous one is answered.
void send_line(const char* s, int timeout = 2000);
void send_linef(const char* fmt, ...);

// Drain the queue. Called every loop().
void send_pump();
bool send_busy();   // an ack is still outstanding

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

// One jog chunk. Never queued: skipped if the controller is behind.
void send_jog_line(const char* s);
void send_jog_cancel();

// Drop everything queued. Lines already written are not recalled.
void send_flush(const char* why);
// Soft-reset the controller (Ctrl-X) and drop everything queued, so nothing
// waiting behind the reset runs after it.
void send_reset();
bool send_reset_was_recent();

// The controller's answers, fed by the parser callbacks.
void cmd_ok();
void cmd_error(int code);
void cmd_alarm(int code);
