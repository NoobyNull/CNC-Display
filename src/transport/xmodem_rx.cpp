#include "transport/xmodem_rx.h"
#include <string.h>

// Receiver for FluidNC's $XS (Xmodem/Send). FluidNC defines TRANSMIT_XMODEM_1K
// (xmodem.cpp:91), so blocks are 1024 bytes with CRC16-CCITT; 128-byte SOH
// blocks are accepted too since the sender falls back on the final short block.

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18

enum rx_state_t { RX_IDLE, RX_SEQ, RX_SEQ_INV, RX_DATA, RX_CRC_HI, RX_CRC_LO };

static xm_tx_fn   s_tx;
static xm_sink_fn s_sink;
static rx_state_t s_state;
static uint8_t    s_buf[1024];
static size_t     s_size, s_pos;
static uint8_t    s_seq, s_seq_inv;
static uint8_t    s_expect;
static uint16_t   s_crc;
static bool       s_done, s_ok;
static size_t     s_bytes;
static int        s_cans;

static uint16_t crc16(const uint8_t* d, size_t n) {
    uint16_t c = 0;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (int k = 0; k < 8; k++) {
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
        }
    }
    return c;
}

void xmodem_rx_begin(xm_tx_fn tx, xm_sink_fn sink) {
    s_tx    = tx;
    s_sink  = sink;
    s_state = RX_IDLE;
    s_size = s_pos = s_bytes = 0;
    s_expect = 1;
    s_done = s_ok = false;
    s_cans = 0;
    if (s_tx) s_tx('C');           // request CRC mode
}

void xmodem_rx_feed(uint8_t c) {
    if (s_done) return;

    if (s_state == RX_IDLE) {
        switch (c) {
            case SOH: s_size = 128;  s_pos = 0; s_state = RX_SEQ; return;
            case STX: s_size = 1024; s_pos = 0; s_state = RX_SEQ; return;
            case EOT:
                s_done = true;
                s_ok   = true;
                if (s_tx) s_tx(ACK);
                return;
            case CAN:
                if (++s_cans >= 2) { s_done = true; s_ok = false; }
                return;
            default:
                s_cans = 0;
                return;
        }
    }
    s_cans = 0;

    switch (s_state) {
        case RX_SEQ:
            s_seq   = c;
            s_state = RX_SEQ_INV;
            return;

        case RX_SEQ_INV:
            s_seq_inv = c;
            s_state   = RX_DATA;
            return;

        case RX_DATA:
            s_buf[s_pos++] = c;
            if (s_pos >= s_size) s_state = RX_CRC_HI;
            return;

        case RX_CRC_HI:
            s_crc   = (uint16_t)c << 8;
            s_state = RX_CRC_LO;
            return;

        case RX_CRC_LO: {
            s_crc |= c;
            s_state = RX_IDLE;

            bool seq_ok = (uint8_t)(s_seq ^ s_seq_inv) == 0xFF;
            bool crc_ok = crc16(s_buf, s_size) == s_crc;

            if (!seq_ok || !crc_ok) {
                if (s_tx) s_tx(NAK);
                return;
            }
            if (s_seq == s_expect) {
                if (s_sink) s_sink(s_buf, s_size);
                s_bytes += s_size;
                s_expect = (uint8_t)(s_expect + 1);
            }
            // A repeat of the previous block means our ACK was lost. Re-ACK it,
            // but do not hand the same bytes to the consumer twice.
            if (s_tx) s_tx(ACK);
            return;
        }

        default:
            s_state = RX_IDLE;
            return;
    }
}

bool   xmodem_rx_done()  { return s_done; }
bool   xmodem_rx_ok()    { return s_ok; }
size_t xmodem_rx_bytes() { return s_bytes; }
