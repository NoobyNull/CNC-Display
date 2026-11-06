#pragma once

// Every machine action the panel can take, as one function each. This is the
// only place G-code and controller commands are composed for an action: pages
// say what to do, ops says how, and each op that is more than one line goes
// out as a sequence (whole or refused, stopped at the first error).
//
// Preconditions (link up, Idle, homed) are the page's to enable or grey out;
// the command layer refuses sequences when there is no link.

// ---- probing ---------------------------------------------------------------
// Sienci AutoZero touch plate. corner 0..3 = bottom left, top left, top right,
// bottom right. floor/centre/lift in mm (pocket floor above stock, pocket
// centre from the corner, final lift above the floor).
void op_autozero(int corner, float floor_mm, float centre_mm, float lift_mm);
// Plain Z probe: down to travel at feed, tool offset, then retract.
void op_probe_z(int feed, int travel_mm, float offset_mm, float retract_mm);

// ---- motion helpers --------------------------------------------------------
// Relative move on one axis, then back to absolute.
void op_move_relative(char axis, float dist_mm);
// Zero the active work system on one axis.
void op_zero_axis(char axis);
// Pulse the stepper driver's reset relay(s). No-op for unknown axes.
void op_driver_reset(char axis);

// ---- work coordinate systems (index 0 = G54 .. 5 = G59) --------------------
void op_wcs_set_here(int idx);      // G10 L20: make the current spot X0 Y0 Z0
void op_wcs_goto_origin(int idx);
void op_wcs_activate(int idx);
void op_wcs_clear_g92();

// ---- tools -----------------------------------------------------------------
void op_tool_load(int t);           // M6 T<t>
void op_tool_unload();              // M6 T0
void op_tool_declare(int t);        // M61: the operator says t is in the spindle
void op_tool_declare_current(int t);// M61 Q0 then Q<t>: atc_manual, zeroed with t
void op_tool_ask();                 // $G and, with the macro, the current tool
void op_tool_ask_macro();           // just the macro's answer
void op_toolsetter_tap();           // does the toolsetter touch something?
