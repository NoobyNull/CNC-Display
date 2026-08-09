#pragma once
#include <stdint.h>
#include <stddef.h>

// Telnet client to FluidNC. FluidNC's Telnet is a raw TCP byte pipe with no IAC
// negotiation (there is no IAC handling anywhere in TelnetClient.cpp), so it is
// binary-safe — XModem toolpath transfers pass through untouched.

void wifilink_begin();      // start/stop according to settings
void wifilink_stop();
void wifilink_poll();       // drive the STA -> discover -> connect state machine

bool wifilink_connected();          // TCP session to FluidNC is up
const char* wifilink_status();      // short human-readable state

void wifilink_putc(uint8_t c);
int  wifilink_getc();               // -1 if nothing buffered
bool wifilink_rx_waiting();
