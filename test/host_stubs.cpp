#include "host_stubs.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_SENT 32
#define SENT_LEN 96

static char s_sent[MAX_SENT][SENT_LEN];
static int  s_nsent = 0;
static int  s_now   = 0;

int         stub_sent_count() { return s_nsent; }
const char* stub_sent(int i)  { return (i >= 0 && i < s_nsent) ? s_sent[i] : ""; }
void        stub_sent_reset() { s_nsent = 0; }
void        stub_set_millis(int ms) { s_now = ms; }

static void record(const char* s) {
    if (s_nsent >= MAX_SENT) return;
    strncpy(s_sent[s_nsent], s, SENT_LEN - 1);
    s_sent[s_nsent][SENT_LEN - 1] = 0;
    s_nsent++;
}

// Matches FluidNCModel.h's declaration, default argument and all.
void send_line(const char* s, int) { record(s ? s : ""); }

void send_linef(const char* fmt, ...) {
    char b[SENT_LEN];
    va_list va;
    va_start(va, fmt);
    vsnprintf(b, sizeof(b), fmt, va);
    va_end(va);
    record(b);
}

// GrblParserC requires the host to supply this; on the panel it is millis().
extern "C" int milliseconds() { return s_now; }
