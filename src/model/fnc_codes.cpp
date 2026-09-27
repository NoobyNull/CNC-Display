#include "model/fnc_codes.h"
#include <stddef.h>

// Transcribed from FluidNC upstream main (2026-09-26): Error.h + Error.cpp
// ErrorNames, Alarm.h + Protocol.cpp AlarmNames. 114 NoData and 115 Deferred
// are internal and never reach a client.
struct code_name_t {
    int         code;
    const char* name;
};

static const code_name_t ERRORS[] = {
    {   1, "Expected GCode command letter" },
    {   2, "Bad GCode number format" },
    {   3, "Invalid $ statement" },
    {   4, "Negative value" },
    {   5, "Setting disabled" },
    {   6, "Step pulse too short" },
    {   7, "Failed to read settings" },
    {   8, "Command requires idle state" },
    {   9, "GCode cannot be executed in lock or alarm state" },
    {  10, "Soft limit error" },
    {  11, "Line too long" },
    {  12, "Max step rate exceeded" },
    {  13, "Check door" },
    {  14, "Line too long" },
    {  15, "Max travel exceeded during jog" },
    {  16, "Invalid jog command" },
    {  17, "Laser mode requires PWM output" },
    {  18, "No Homing/Cycle defined in settings" },
    {  19, "Single axis homing not allowed" },
    {  20, "Unsupported GCode command" },
    {  21, "Gcode modal group violation" },
    {  22, "Gcode undefined feed rate" },
    {  23, "Gcode command value not integer" },
    {  24, "Gcode axis command conflict" },
    {  25, "Gcode word repeated" },
    {  26, "Gcode no axis words" },
    {  27, "Gcode invalid line number" },
    {  28, "Gcode value word missing" },
    {  29, "Gcode unsupported coordinate system" },
    {  30, "Gcode G53 invalid motion mode" },
    {  31, "Gcode extra axis words" },
    {  32, "Gcode no axis words in plane" },
    {  33, "Gcode invalid target" },
    {  34, "Gcode arc radius error" },
    {  35, "Gcode no offsets in plane" },
    {  36, "Gcode unused words" },
    {  37, "Gcode G43 dynamic axis error" },
    {  38, "Gcode max value exceeded" },
    {  39, "P param max exceeded" },
    {  40, "Check startup pins" },
    {  60, "Failed to mount device" },
    {  61, "Read failed" },
    {  62, "Failed to open directory" },
    {  63, "Directory not found" },
    {  64, "File empty" },
    {  65, "File not found" },
    {  66, "Failed to open file" },
    {  67, "Device is busy" },
    {  68, "Failed to delete directory" },
    {  69, "Failed to delete file" },
    {  70, "Failed to rename file" },
    {  80, "Number out of range for setting" },
    {  81, "Invalid value for setting" },
    {  82, "Failed to create file" },
    {  83, "Failed to format filesystem" },
    {  90, "Failed to send message" },
    { 100, "Failed to store setting" },
    { 101, "Failed to get setting status" },
    { 110, "Authentication failed!" },
    { 111, "End of line" },
    { 112, "End of file" },
    { 113, "System Reset" },
    { 120, "Another interface is busy" },
    { 130, "Jog Cancelled" },
    { 150, "Bad Pin Specification" },
    { 151, "Bad Runtime Config Setting" },
    { 152, "Configuration is invalid. Check boot messages for ERR's." },
    { 160, "File Upload Failed" },
    { 161, "File Download Failed" },
    { 162, "Read-only setting" },
    { 170, "Expression Divide By Zero" },
    { 171, "Expression Invalid Argument" },
    { 173, "Expression Unknown Operator" },
    { 174, "Expression Argument Out of Range" },
    { 175, "Expression Syntax Error" },
    { 176, "Flow Control Syntax Error" },
    { 177, "Flow Control Not Executing Macro" },
    { 178, "Flow Control Out of Memory" },
    { 179, "Flow Control Stack Overflow" },
    { 180, "Parameter Assignment Failed" },
    { 181, "Gcode invalid word value" },
};

static const code_name_t ALARMS[] = {
    {   1, "Hard Limit" },
    {   2, "Soft Limit" },
    {   3, "Abort Cycle" },
    {   4, "Probe Fail Initial" },
    {   5, "Probe Fail Contact" },
    {   6, "Homing Fail Reset" },
    {   7, "Homing Fail Door" },
    {   8, "Homing Fail Pulloff" },
    {   9, "Homing Fail Approach" },
    {  10, "Spindle Control" },
    {  11, "Input Pin Initially On" },
    {  12, "Ambiguous Switch" },
    {  13, "Hard Stop" },
    {  14, "Unhomed" },
    {  15, "Init" },
    {  16, "Expander Reset" },
    {  17, "GCode Error" },
    {  18, "Probe Hard Limit" },
};

template <size_t N>
static const char* lookup(const code_name_t (&t)[N], int code) {
    for (const auto& e : t)
        if (e.code == code) return e.name;
    return nullptr;
}

const char* fnc_error_name(int code) { return lookup(ERRORS, code); }
const char* fnc_alarm_name(int code) { return lookup(ALARMS, code); }
