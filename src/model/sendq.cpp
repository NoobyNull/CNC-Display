#include "model/sendq.h"

#include <string.h>

void sendq_reset(sendq_t* q) {
    q->head    = 0;
    q->count   = 0;
    q->dropped = 0;
}

bool sendq_push(sendq_t* q, const char* s) {
    if (!s) return false;
    if (q->count >= SENDQ_SLOTS) {
        // Drop the NEWEST, not the oldest. The queue is ordered and the lines
        // in it are often a sequence -- dropping from the middle would leave a
        // "G10 L20 ..." without its "$#", which is worse than losing the tail.
        ++q->dropped;
        return false;
    }
    const uint8_t slot = (uint8_t)((q->head + q->count) % SENDQ_SLOTS);
    strncpy(q->line[slot], s, SENDQ_LINE - 1);
    q->line[slot][SENDQ_LINE - 1] = '\0';
    ++q->count;
    return true;
}

bool sendq_pop(sendq_t* q, char* out, size_t out_len) {
    if (q->count == 0 || !out || out_len == 0) return false;
    strncpy(out, q->line[q->head], out_len - 1);
    out[out_len - 1] = '\0';
    q->head = (uint8_t)((q->head + 1) % SENDQ_SLOTS);
    --q->count;
    return true;
}

size_t   sendq_count(const sendq_t* q)   { return q->count; }
uint32_t sendq_dropped(const sendq_t* q) { return q->dropped; }
