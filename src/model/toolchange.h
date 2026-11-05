#pragma once

// The job asked for a tool. Three sources, all reaching the panel as [MSG:]:
//   the post         "(MSG, Change to T2: <name>)"         -> the tool's name
//   atc_manual       "Install tool #2 then resume ..."     -> install, measured after
//   openatc_m6.nc    "Install tool T2 then resume ..."     -> install, measured after
//                    "Remove tool T6 then resume ..."      -> take it out; the rack
//                    "Remove the tool in the spindle ..."     does the rest
// Remembered until the pause ends so the UI can say WHICH tool instead of HOLD.

void        tc_note_message(const char* args);   // [MSG:...] arguments
bool        tc_pending();    // a request is waiting for the operator
int         tc_tool();       // the tool to install or remove; -1 = unknown
bool        tc_remove();     // true: take it OUT (the next one comes from the rack)
const char* tc_name();       // tool name from the post's message, "" if none
bool        tc_measured();   // resume measures it on the toolsetter and sets TLO
void        tc_clear();

// The tool FluidNC itself says is in the spindle. $G only gives the SELECTED
// tool, which moves the moment M6 is read and stays there if the change dies;
// the status report has no tool at all. So the panel asks with a PRINT, and the
// reply lands here. -1 = FluidNC does not know, as after every boot and reset.
#define TC_ASK_CURRENT "(PRINT,TOOLNOW %d#<_current_tool>)"
int         tc_current();
bool        tc_current_known();      // the controller has answered at least once
void        tc_assume(int t);        // the panel just set it with M61: no need to wait

// Nothing in the spindle. T0 is the custom tool on this machine, so empty needs a
// number of its own; 98 is what RapidChange's macros use for the same thing.
#define TC_EMPTY 98

// openatc_tap.nc's answer: TC_TAP_NONE until one arrives.
enum { TC_TAP_NONE = -1, TC_TAP_EMPTY = 0, TC_TAP_TOOL = 1 };
int         tc_tap();
void        tc_tap_clear();
