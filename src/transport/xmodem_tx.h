#pragma once
#include <stdint.h>
#include <stddef.h>

// XModem-1K transmitter, the mirror of xmodem_rx. Used to push a file from the
// panel's SD card into FluidNC via "$XR=<path>", which opens the path for
// writing and pauses its status polling for the duration
// (FluidNC FileCommands.cpp:589).
//
// Data is pulled through a callback rather than held in RAM: a 1MB job would
// not fit in a buffer worth allocating.
typedef void   (*xt_tx_fn)(uint8_t c);
typedef size_t (*xt_read_fn)(uint8_t* dst, size_t max);   // 0 = end of file

void xmodem_tx_begin(xt_tx_fn tx, xt_read_fn reader);
void xmodem_tx_feed(uint8_t c);    // feed bytes received from the far end
void xmodem_tx_tick(uint32_t now_ms);

bool   xmodem_tx_done();
bool   xmodem_tx_ok();
size_t xmodem_tx_bytes();
int    xmodem_tx_blocks();
