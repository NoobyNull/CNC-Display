#pragma once
#include <stdint.h>

// Raw IMU recorder. The vibration guard keeps only |a| and throws the three
// axes away every sample (hw/imu.cpp), which is enough to notice a machine
// shaking and useless for working out what is shaking: a scalar magnitude
// cannot tell an X reversal from a Z chatter, and cannot be rotated into the
// machine frame afterwards. This keeps the axes.
//
// The sensor on this machine is bolted to the spindle in an arbitrary
// orientation, so the frame is recovered in post rather than assumed: the
// still baseline at the head of every log gives gravity (= "down" in the
// sensor frame) and one known single-axis move gives a second reference.
// That only works on raw axes, which is the whole reason this exists.
//
// Two sinks, and they are independent because the panel can only ever offer
// one of them at a time (see transport/uart.h — the FluidNC link and the USB
// console are the same two pins):
//
//   SD    the real one. Runs with the link up, so every sample carries the
//         machine state and position it happened at.
//   TTY   115200 console on the USB-C, alive only when the Setup "Wired UART"
//         toggle is OFF. No link, so no position — but the host PC can talk to
//         FluidNC over its own USB cable at the same time and hold that end of
//         the correlation itself.

#define IMULOG_AXES 4        // X Y Z A, matching this machine

bool imulog_start();         // opens the next /imu/NNNN.csv; false if no card
void imulog_stop();
bool imulog_active();

// Serial echo, independent of whether a card is recording. A no-op in link
// mode, where Serial is never begun.
void imulog_set_tty(bool on);
bool imulog_tty();

// Fed from imu_poll() with the sample it just took. Cheap by design: it copies
// into a ring and returns, because it runs inside loop() between LVGL frames.
void imulog_sample(uint32_t t_us, float ax, float ay, float az);

// Drains the ring to the card. Must be called from loop(), never from a UI
// callback — an SD write can stall for tens of milliseconds and the FluidNC RX
// ring is only 16K (about 160 ms at 1 Mbaud).
void imulog_poll();

// One short line for the glass: "REC 12.4s 2480" / "3 files" / "no card".
const char* imulog_status();
const char* imulog_path();

uint32_t imulog_count();     // samples written this session
uint32_t imulog_dropped();   // samples lost because the ring was full
