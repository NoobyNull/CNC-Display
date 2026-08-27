#pragma once
#include <stddef.h>

// What this machine actually has, read from the controller instead of assumed.
//
// The console used to draw a fixed page set: a PROBE page on a machine with no
// probe, a MACRO page with six buttons on a firmware that has four slots. Every
// one of those is a control that reports an error when pressed, which trains an
// operator to ignore errors.
//
// Source is `$CD` — the config dump — NOT `$S`. On FluidNC v4 `$S` returns only
// the Grbl-compat settings (`$Grbl/MaxRate/X` and friends) and carries no
// config tree at all, so anything deciding "is there a probe" from `$S` reads
// "no" for ever on v4. That is what left the Diag probe lamp dead.
//
// Three-valued on purpose, and the three are NOT collapsed by callers into
// two the same way everywhere. UNKNOWN means the controller has not been heard
// from or the dump never arrived — it is an absence of knowledge, not of
// hardware. The UI treats UNKNOWN as "do not offer this" for pages (see
// page_available) and as "warn anyway" for the limits banner: for a control,
// unknown means do not tempt; for a hazard, unknown means do not reassure.
enum cap_state_t { CAP_UNKNOWN = 0, CAP_NO, CAP_YES };

// FluidNC v4 defines Macro0..Macro3. Not six, which is what the console drew.
#define CAPS_MACRO_MAX 4
#define CAPS_MACRO_NAME_MAX 24

void caps_reset();

// Queue the `$CD` request. Safe to call once the link is up; the reply is
// consumed by caps_feed_line().
void caps_request();

// Fed every unrecognised controller line. Returns true when the line belonged
// to the config dump, so the caller swallows it: `$CD` is ~245 lines and the
// console ring holds 120, so letting it through would erase the log every time.
bool caps_feed_line(const char* line);

// Call once per main-loop pass. Ends a dump on quiescence and gives up if one
// never starts, so nothing waits on caps_settled() for ever.
void caps_tick();

cap_state_t caps_probe();
cap_state_t caps_homing();
cap_state_t caps_macros();

// Whether ANY axis has soft limits or ANY motor has hard limits enabled.
// CAP_NO means the controller described a whole machine and not one axis of it
// is protected — a mistyped goto or an over-long jog then runs into the frame
// with nothing to stop it.
cap_state_t caps_limits();

// True once a dump has completed, or the request has been given up on. The
// boot gate waits for this so the page set is decided before the UI is shown
// rather than rearranging itself under the operator.
bool caps_settled();

// Non-empty macro slots only, in controller order. count is 0..CAPS_MACRO_MAX.
size_t      caps_macro_count();
const char* caps_macro_name(size_t i);   // the key, e.g. "Macro0"
const char* caps_macro_body(size_t i);   // the g-code, for the button subtitle
