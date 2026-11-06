#include "ops/ops.h"
#include "cmd/cmd.h"
#include "model/FluidNCModel.h"
#include "model/caps.h"
#include "model/toolchange.h"
#include <stdio.h>
#include <string.h>

// ---- probing ---------------------------------------------------------------

// Sequence from gSender's get3AxisAutoRoutine (src/app/src/lib/Probing.ts):
// the bit sits in the plate's pocket, touches the pocket floor for Z, then
// both walls in X and in Y. gSender does the centring maths itself between
// sends; here FluidNC does it, reading each touch from #5061/#5062 (probe
// position, machine coordinates) and driving to the midpoint with G53, so the
// whole routine is one stream of lines and bit diameter never enters into it.
// Pocket centre relative to the corner, per corner (gSender determineAutoPlateOffsetValues).
static const int8_t CORNER_SX[4] = { 1, 1, -1, -1 };
static const int8_t CORNER_SY[4] = { 1, -1, -1, 1 };

void op_autozero(int c, float floor_mm, float centre_mm, float lift_mm) {
    if (c < 0 || c > 3) return;
    static const char* const WALLS[] = {
        // Z: pocket floor, fast then slow.
        "G21 G91",
        "G38.2 Z-25 F200",
        "G0 Z2",
        "G38.2 Z-5 F75",
        nullptr,   // G10 Z, filled below
        "G0 Z3",
        // X: left wall, right wall, centre between them.
        "G0 X-13",
        "G38.2 X-30 F150",
        "G0 X2",
        "G38.2 X-5 F75",
        "#<az_a>=#5061",
        "G0 X26",
        "G38.2 X30 F150",
        "G0 X-2",
        "G38.2 X5 F75",
        "G90 G53 G0 X[[#<az_a>+#5061]/2]",
        // Y: same again.
        "G91 G0 Y-13",
        "G38.2 Y-30 F150",
        "G0 Y2",
        "G38.2 Y-5 F75",
        "#<az_a>=#5062",
        "G0 Y26",
        "G38.2 Y30 F150",
        "G0 Y-2",
        "G38.2 Y5 F75",
        "G90 G53 G0 Y[[#<az_a>+#5062]/2]",
    };
    // One sequence, 28 lines: queued whole or refused, and stopped at the
    // first error or alarm (a missed wall is ALARM:5) instead of probing on
    // from the wrong place. Stopped early it still leaves G91 behind, hence
    // the cleanup line.
    seq_begin("AutoZero");
    seq_cleanup("G90");
    for (const char* l : WALLS) {
        if (l) seq_line(l);
        else   seq_linef("G10 L20 P0 Z%.3f", (double)floor_mm);
    }
    seq_linef("G10 L20 P0 X%.3f Y%.3f", (double)(CORNER_SX[c] * centre_mm), (double)(CORNER_SY[c] * centre_mm));
    // Out of the pocket before anything moves sideways; X/Y stay put so the
    // plate can be lifted off. gSender's final X0Y0 move is left to GOTO.
    seq_linef("G90 G0 Z%.3f", (double)(floor_mm + lift_mm));
    seq_send();
}

// Sequence from FluidDial's ProbingScene: probe down, then retract.
void op_probe_z(int feed, int travel_mm, float offset_mm, float retract_mm) {
    // G91 on the probe line is modal: without the trailing G90 the parser was
    // left relative, and the next absolute move from any page went wrong.
    seq_begin("Probe");
    seq_cleanup("G90");
    seq_linef("G38.2G91F%dZ-%dP%.2f", feed, travel_mm, (double)offset_mm);
    seq_line("G90");
    seq_linef("$J=G91F1000Z%.2f", (double)retract_mm);
    seq_send();
}

// ---- motion helpers --------------------------------------------------------

// G91 for the move, then straight back to G90: leaving the parser in relative
// mode is how the next absolute move from any other page goes somewhere absurd.
void op_move_relative(char axis, float dist_mm) {
    seq_begin("Calibration move");
    seq_cleanup("G90");
    seq_linef("G91 G0 %c%.3f", axis, (double)dist_mm);
    seq_line("G90");
    seq_send();
}

// G10 L20 P0 targets the *active* work system, which is why the DRO shows
// which one is live on every page.
void op_zero_axis(char axis) {
    send_linef("G10 L20 P0 %c0", axis);
}

// Pulse the driver's reset relay(s). M64/M65 P<n> addresses the config's
// user_outputs SLOT, not the Airedale pin: slots 0-4 map to Airedale
// outputs 8-12 (X Y1 Y2 Z A), each a normally-closed relay in that
// driver's power feed. ON -> dwell -> OFF power-cycles the driver, which
// is how a latched fault clears. The dwell runs on the controller as G4,
// so nothing here blocks; both Y relays pulse together — half a gantry
// with power is worse than none.
void op_driver_reset(char ax) {
    int outs[2] = { -1, -1 };
    switch (ax) {
        case 'X': outs[0] = 0;              break;
        case 'Y': outs[0] = 1; outs[1] = 2; break;
        case 'Z': outs[0] = 3;              break;
        case 'A': outs[0] = 4;              break;
        default:  return;
    }
    // One sequence: a relay switched on must never be left on because the
    // line that switches it off was dropped.
    char off[32] = "";
    seq_begin("Driver reset");
    for (int i = 0; i < 2; i++)
        if (outs[i] >= 0) seq_linef("M64 P%d", outs[i]);
    seq_line("G4 P0.75");   // relay drop + driver discharge; tune on bench
    for (int i = 0; i < 2; i++)
        if (outs[i] >= 0) {
            seq_linef("M65 P%d", outs[i]);
            const size_t n = strlen(off);
            snprintf(off + n, sizeof(off) - n, "%sM65 P%d", n ? "\n" : "", outs[i]);
        }
    seq_cleanup(off);
    seq_send();
}

// ---- work coordinate systems -----------------------------------------------

void op_wcs_set_here(int idx) {
    if (idx < 0 || idx > 5) return;   // only G54..G59 are settable this way
    send_linef("G10 L20 P%d X0 Y0 Z0", idx + 1);
    send_line("$#");
}

void op_wcs_goto_origin(int idx) {
    if (idx < 0 || idx > 5) return;
    send_linef("G%d G0 X0 Y0", 54 + idx);
}

void op_wcs_activate(int idx) {
    if (idx < 0 || idx > 5) return;
    send_linef("G%d", 54 + idx);
    send_line("$#");
}

void op_wcs_clear_g92() {
    send_line("G92.1");
    send_line("$#");
}

// ---- tools -----------------------------------------------------------------

void op_tool_load(int t)   { send_linef("M6 T%d", t); }
void op_tool_unload()      { send_line("M6 T0"); }

void op_tool_declare(int t) {
    send_linef("M61 Q%d", t);
    send_line(TC_ASK_CURRENT);
    tc_assume(t);
}

void op_tool_declare_current(int t) {
    send_line("M61 Q0");
    send_linef("M61 Q%d", t);
}

void op_tool_ask_macro()   { send_line(TC_ASK_CURRENT); }

void op_tool_ask() {
    send_line("$G");
    if (caps_atc() == CAPS_ATC_MACRO) send_line(TC_ASK_CURRENT);
}

void op_toolsetter_tap()   { send_line("$LocalFS/Run=openatc_tap.nc"); }
