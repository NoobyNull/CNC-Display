#pragma once

// Names for FluidNC's error:N and ALARM:N codes, so the console reads
// "error:22 Gcode undefined feed rate" rather than a bare number. FluidNC's
// numbers overlap Grbl's but not always their meanings (10, 17, 18, 19), and
// this panel only ever talks to FluidNC, so this is FluidNC's table alone.
// nullptr for a code the table does not know.
const char* fnc_error_name(int code);
const char* fnc_alarm_name(int code);
