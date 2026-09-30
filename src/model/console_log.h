#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define CLOG_CAPACITY 120
#define CLOG_LINE_MAX 96

enum clog_kind_t { CLOG_SENT, CLOG_OK, CLOG_ERROR, CLOG_ALARM, CLOG_MSG, CLOG_INFO, CLOG_STATUS };

struct clog_line_t {
    char        text[CLOG_LINE_MAX];
    clog_kind_t kind;
};

void clog_reset();
void clog_add(const char* line);
void clog_set_show_status(bool on);
bool clog_show_status();

size_t             clog_count();
const clog_line_t* clog_line(size_t i);   // 0 = oldest retained

// Absolute numbering, for a caller waiting on the reply to its own command:
// note clog_total() when sending, then read lines from that number on. A line
// that has already scrolled out of the ring returns nullptr.
uint32_t           clog_total();
const clog_line_t* clog_line_abs(uint32_t n);
bool               clog_dirty_take();
