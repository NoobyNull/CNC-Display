#include "model/model_events.h"
#include "model/console_log.h"
#include "model/offsets.h"
#include "model/filelist.h"
#include "model/fnc_settings.h"
#include "model/caps.h"
#include <string.h>
#include <strings.h>   // strcasecmp
#include <stdio.h>

static bool s_dro = false, s_state = false, s_limits = false;

// Sticky, unlike s_dro which is consumed by the UI each tick. "Has the
// controller ever described itself?" is a different question from "is there a
// redraw pending", and the boot gate needs the former: n_axes reads 3 from its
// initialiser long before any report arrives, so it cannot be used to tell
// "3-axis machine" from "nobody has spoken yet".
static bool s_dro_ever = false;

bool model_dro_ever() { return s_dro_ever; }
static char s_error[96] = { 0 };
static bool s_error_pending = false;

void model_flag_dro()    { s_dro = true; s_dro_ever = true; }
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

// The pendant cable runs through a passthrough board (an Airedale, here) that
// is electrically transparent: it has no address, answers no command, and
// cannot be probed. The ONLY direct evidence it exists is that FluidNC names it
// once at boot — "IO Expander BOARD:Airedale v1.1". Latched here because the
// console log is a 120-line ring and the banner scrolls out of it within
// seconds. Matched on "BOARD:" rather than on the word Airedale so a different
// expander reports itself instead of showing as absent.
static char s_board[48] = { 0 };

const char* model_expander_board() { return s_board[0] ? s_board : nullptr; }

static void sniff_board(const char* line) {
    if (s_board[0] || !line) return;
    const char* p = strstr(line, "BOARD:");
    if (!p) return;
    p += 6;
    size_t n = 0;
    while (p[n] && p[n] != ']' && p[n] != '\r' && p[n] != '\n' && n < sizeof(s_board) - 1) n++;
    memcpy(s_board, p, n);
    s_board[n] = 0;
}

// Remote "update yourself": [MSG:PANEL:UPDATE] from the controller, e.g.
// telnet to FluidNC and send  $LM=*PANEL:UPDATE  (broadcast) or
// $Msg/Channel=uart_channel2,PANEL:UPDATE. Only raised here; main.cpp decides
// whether the machine is in a state to act on it.
static bool s_update_req = false;
bool model_take_update_request() { bool r = s_update_req; s_update_req = false; return r; }

void model_flag_message(const char* cmd, const char* args) {
    if (cmd && args && !strcasecmp(cmd, "PANEL") && !strcasecmp(args, "UPDATE")) s_update_req = true;
    char buf[CLOG_LINE_MAX];
    snprintf(buf, sizeof(buf), "[MSG:%s %s]", cmd ? cmd : "", args ? args : "");
    sniff_board(buf);
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

// ui/page_diag.cpp — declared here bare to keep lvgl out of the model layer.
bool page_diag_limits_feed(const char* line);

// Consumers, none of which should know about the others: the Diag limit
// monitor (which swallows its 2-lines-a-second stream), the work-offset table
// ($# replies), the file list (JSON directory replies) and the console. Each
// ignores what is not addressed to it.
extern "C" void handle_other_line(char* line) {
    if (!line) return;
    sniff_board(line);   // the banner arrives as a plain line, not only as [MSG:]
    // The limit monitor gets first refusal: its stream is addressed to Diag,
    // and caps must never consume it (doing so blanked the limit lamps AND
    // stopped the config dump from ever ending).
    if (page_diag_limits_feed(line)) return;
    // The $CD config dump is ~300 lines of YAML. It answers "does this machine
    // have a probe / coolant / door" and belongs to the capability model, not
    // to the console it would otherwise bury.
    if (caps_feed_line(line)) return;
    offsets_feed_line(line);
    fncset_feed_line(line);
    filelist_feed_line(line);
    clog_add(line);
}
