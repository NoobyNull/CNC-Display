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
