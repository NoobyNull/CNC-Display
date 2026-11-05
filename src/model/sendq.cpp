#include "model/sendq.h"

#include <string.h>

void sendq_reset(sendq_t* q) {
    q->head    = 0;
    q->count   = 0;
    q->dropped = 0;
}

static void put(sendq_t* q, const char* s, sendq_seq_t seq) {
    const uint8_t slot = (uint8_t)((q->head + q->count) % SENDQ_SLOTS);
    strncpy(q->line[slot], s, SENDQ_LINE - 1);
    q->line[slot][SENDQ_LINE - 1] = '\0';
    q->seq[slot] = seq;
    ++q->count;
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
    put(q, s, 0);
    return true;
}

bool sendq_push_all(sendq_t* q, const char* const* lines, int n, sendq_seq_t seq) {
    if (!lines || n <= 0 || seq == 0) return false;
    if ((size_t)n > sendq_free(q)) return false;
    for (int i = 0; i < n; i++)
        if (!lines[i]) return false;
    for (int i = 0; i < n; i++) put(q, lines[i], seq);
    return true;
}

bool sendq_pop_seq(sendq_t* q, char* out, size_t out_len, sendq_seq_t* seq) {
    if (q->count == 0 || !out || out_len == 0) return false;
    strncpy(out, q->line[q->head], out_len - 1);
    out[out_len - 1] = '\0';
    if (seq) *seq = q->seq[q->head];
    q->head = (uint8_t)((q->head + 1) % SENDQ_SLOTS);
    --q->count;
    return true;
}

bool sendq_pop(sendq_t* q, char* out, size_t out_len) {
    return sendq_pop_seq(q, out, out_len, nullptr);
}

// Compacts in place: walk every slot, keep the ones that stay, in order.
static int drop_where(sendq_t* q, bool all, sendq_seq_t seq) {
    int kept = 0, removed = 0;
    for (int i = 0; i < q->count; i++) {
        const uint8_t from = (uint8_t)((q->head + i) % SENDQ_SLOTS);
        const sendq_seq_t s = q->seq[from];
        if (s != 0 && (all || s == seq)) { ++removed; continue; }
        const uint8_t to = (uint8_t)((q->head + kept) % SENDQ_SLOTS);
        if (to != from) {
            memcpy(q->line[to], q->line[from], SENDQ_LINE);
            q->seq[to] = s;
        }
        ++kept;
    }
    q->count = (uint8_t)kept;
    return removed;
}

int sendq_drop_seq(sendq_t* q, sendq_seq_t seq) { return seq ? drop_where(q, false, seq) : 0; }
int sendq_drop_all_seqs(sendq_t* q)            { return drop_where(q, true, 0); }

size_t sendq_free(const sendq_t* q) { return SENDQ_SLOTS - q->count; }

size_t   sendq_count(const sendq_t* q)   { return q->count; }
uint32_t sendq_dropped(const sendq_t* q) { return q->dropped; }

int sendq_count_seq(const sendq_t* q, sendq_seq_t seq) {
    int n = 0;
    for (int i = 0; i < q->count; i++)
        if (q->seq[(q->head + i) % SENDQ_SLOTS] == seq) ++n;
    return n;
}
