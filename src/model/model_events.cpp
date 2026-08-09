#include "model/model_events.h"
#include "model/console_log.h"
#include "model/offsets.h"
#include "model/filelist.h"
#include <string.h>
#include <stdio.h>

static bool s_dro = false, s_state = false, s_limits = false;
static char s_error[96] = { 0 };
static bool s_error_pending = false;

void model_flag_dro()    { s_dro = true; }
void model_flag_state()  { s_state = true; }
void model_flag_limits() { s_limits = true; }

void model_flag_error(const char* msg) {
    if (!msg) return;
    strncpy(s_error, msg, sizeof(s_error) - 1);
    s_error[sizeof(s_error) - 1] = 0;
    s_error_pending = true;

    char buf[CLOG_LINE_MAX];
    snprintf(buf, sizeof(buf), "error: %s", msg);
    clog_add(buf);
}

void model_flag_message(const char* cmd, const char* args) {
    char buf[CLOG_LINE_MAX];
    snprintf(buf, sizeof(buf), "[MSG:%s %s]", cmd ? cmd : "", args ? args : "");
    clog_add(buf);
}

bool model_take_dro()    { bool d = s_dro;    s_dro    = false; return d; }
bool model_take_state()  { bool d = s_state;  s_state  = false; return d; }
bool model_take_limits() { bool d = s_limits; s_limits = false; return d; }

const char* model_take_error() {
    if (!s_error_pending) return nullptr;
    s_error_pending = false;
    return s_error;
}

// Three consumers, none of which should know about the others: the work-offset
// table ($# replies), the file list (JSON directory replies) and the console.
// Each ignores what is not addressed to it.
extern "C" void handle_other_line(char* line) {
    if (!line) return;
    offsets_feed_line(line);
    filelist_feed_line(line);
    clog_add(line);
}
