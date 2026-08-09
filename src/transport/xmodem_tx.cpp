#include "transport/xmodem_tx.h"
#include <string.h>

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define SUB 0x1A

#define BLOCK 1024          // XModem-1K; FluidNC's receiver accepts both sizes
#define MAX_RETRY 10

enum tx_state_t { TX_WAIT_START, TX_SENDING, TX_WAIT_ACK, TX_WAIT_EOT_ACK, TX_DONE };

static xt_tx_fn   s_tx;
static xt_read_fn s_read;
static tx_state_t s_state;
static uint8_t    s_buf[BLOCK];
static size_t     s_len;
static uint8_t    s_seq;
static bool       s_crc_mode;
static bool       s_eof;
static bool       s_ok;
static size_t     s_bytes;
static int        s_blocks;
static int        s_retry;
static uint32_t   s_deadline;

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

// Load the next block. Short final blocks are padded with CTRL-Z, which is what
// every XModem implementation expects — the receiver strips it.
static bool load_block() {
    if (s_eof) return false;
    s_len = s_read ? s_read(s_buf, BLOCK) : 0;
    if (s_len == 0) { s_eof = true; return false; }
    if (s_len < BLOCK) memset(s_buf + s_len, SUB, BLOCK - s_len);
    return true;
}

static void send_block() {
    if (!s_tx) return;
    s_tx(STX);
    s_tx(s_seq);
    s_tx((uint8_t)~s_seq);
    for (size_t i = 0; i < BLOCK; i++) s_tx(s_buf[i]);
    if (s_crc_mode) {
        uint16_t c = crc16(s_buf, BLOCK);
        s_tx((uint8_t)(c >> 8));
        s_tx((uint8_t)(c & 0xFF));
    } else {
        uint8_t sum = 0;
        for (size_t i = 0; i < BLOCK; i++) sum = (uint8_t)(sum + s_buf[i]);
        s_tx(sum);
    }
}

void xmodem_tx_begin(xt_tx_fn tx, xt_read_fn reader) {
    s_tx = tx; s_read = reader;
    s_state = TX_WAIT_START;
    s_seq = 1;
    s_crc_mode = true;
    s_eof = false;
    s_ok = false;
    s_bytes = 0;
    s_blocks = 0;
    s_retry = 0;
    s_len = 0;
    s_deadline = 0;
}

void xmodem_tx_feed(uint8_t c) {
    if (s_state == TX_DONE) return;

    if (c == CAN) {
        s_state = TX_DONE;
        s_ok = false;
        return;
    }

    switch (s_state) {
        case TX_WAIT_START:
            // 'C' asks for CRC-16, NAK asks for the old checksum. Either starts us.
            if (c != 'C' && c != NAK) return;
            s_crc_mode = (c == 'C');
            if (!load_block()) {          // empty file: nothing to send
                if (s_tx) s_tx(EOT);
                s_state = TX_WAIT_EOT_ACK;
                return;
            }
            send_block();
            s_retry = 0;
            s_state = TX_WAIT_ACK;
            return;

        case TX_WAIT_ACK:
            if (c == ACK) {
                s_bytes += s_len;
                s_blocks++;
                s_seq = (uint8_t)(s_seq + 1);
                s_retry = 0;
                if (load_block()) {
                    send_block();
                } else {
                    if (s_tx) s_tx(EOT);
                    s_state = TX_WAIT_EOT_ACK;
                }
            } else if (c == NAK) {
                if (++s_retry > MAX_RETRY) { s_state = TX_DONE; s_ok = false; return; }
                send_block();             // same sequence number, resend
            }
            return;

        case TX_WAIT_EOT_ACK:
            if (c == ACK) {
                s_state = TX_DONE;
                s_ok = true;
            } else if (c == NAK) {
                if (++s_retry > MAX_RETRY) { s_state = TX_DONE; s_ok = false; return; }
                if (s_tx) s_tx(EOT);
            }
            return;

        default:
            return;
    }
}

void xmodem_tx_tick(uint32_t now_ms) {
    // The far end went quiet mid-transfer. Give up rather than hang the UI
    // forever waiting for an ACK that is not coming.
    if (s_state == TX_DONE || s_state == TX_WAIT_START) return;
    if (s_deadline == 0) { s_deadline = now_ms + 10000; return; }
    if ((int32_t)(now_ms - s_deadline) >= 0) {
        s_state = TX_DONE;
        s_ok = false;
    }
}

bool   xmodem_tx_done()   { return s_state == TX_DONE; }
bool   xmodem_tx_ok()     { return s_ok; }
size_t xmodem_tx_bytes()  { return s_bytes; }
int    xmodem_tx_blocks() { return s_blocks; }
