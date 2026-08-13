#pragma once
#include <stddef.h>
#include <stdint.h>

// FIFO of controller lines waiting to go out.
//
// fnc_send_line() waits for the PREVIOUS line's ack before writing the next --
// up to its full 2000ms timeout -- and every UI caller runs inside
// lv_timer_handler(). So an inline send freezes the whole panel, touch
// included. Measured on hardware: 8.4s of dead UI at link-up, from the three
// priming lines alone.
//
// Queueing here and draining from loop() means a slow or silent controller
// costs latency on the wire instead of a frozen screen. Order is preserved,
// which matters: page_offsets sends "G10 L20 ..." then "$#", and page_cal
// sends "G91 G0 ..." then "G90".
//
// Free of LVGL and the link so the wraparound and the overflow policy can be
// tested on the host.

#define SENDQ_SLOTS 16
#define SENDQ_LINE  128   // matches vsend_linef()'s buffer

typedef struct {
    char     line[SENDQ_SLOTS][SENDQ_LINE];
    uint8_t  head;      // next slot to pop
    uint8_t  count;     // slots in use
    uint32_t dropped;   // lines lost to a full queue, ever
} sendq_t;

void sendq_reset(sendq_t* q);

// Copies s in, truncating at SENDQ_LINE-1. False means the queue was full and
// the line was DROPPED -- a machine command silently vanishing, so the caller
// is expected to say so rather than ignore the return.
bool sendq_push(sendq_t* q, const char* s);

// Copies the oldest line into out. False if empty. Copies rather than lending
// a pointer into the ring, so a push cannot move the caller's line underneath
// it.
bool sendq_pop(sendq_t* q, char* out, size_t out_len);

size_t   sendq_count(const sendq_t* q);
uint32_t sendq_dropped(const sendq_t* q);
