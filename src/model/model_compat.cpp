#include "model/model_compat.h"
#include "model/model_events.h"
#include "model/console_log.h"
#include "model/sys_compat.h"
#include <stdarg.h>
#include <stdio.h>

// --- redraw ----------------------------------------------------------------

static bool s_redisplay = false;

void request_redisplay() { s_redisplay = true; }

bool take_redisplay() {
    bool d = s_redisplay;
    s_redisplay = false;
    return d;
}

void act_on_state_change() {
    model_flag_state();
    request_redisplay();
}

// --- boot log --------------------------------------------------------------

void bootlog_printf(const char* fmt, ...) {
    char    buf[160];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    dbg_print("[boot] ");
    dbg_println(buf);
    clog_add(buf);
}
