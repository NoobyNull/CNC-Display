#pragma once
#include <stdint.h>
#include <stddef.h>

typedef void (*xm_tx_fn)(uint8_t c);
typedef void (*xm_sink_fn)(const uint8_t* data, size_t len);

// Start a receive. Emits 'C' to request CRC mode.
void xmodem_rx_begin(xm_tx_fn tx, xm_sink_fn sink);

// Feed one received byte.
void xmodem_rx_feed(uint8_t c);

bool   xmodem_rx_done();    // EOT seen, or cancelled
bool   xmodem_rx_ok();      // completed successfully
size_t xmodem_rx_bytes();   // payload bytes delivered so far
