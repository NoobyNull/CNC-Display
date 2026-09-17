#pragma once
// Desktop stand-in for the Arduino core, for [env:sim] only. This directory is
// on the include path ONLY for the simulator (-I src/sim), so the firmware
// build never sees it and still gets the real core.
//
// The whole surface the UI and model layers actually use is millis(), delay()
// and four Serial methods — grep says so. Anything beyond that belongs in a
// hardware stub (sim_hw.cpp), not here.

// The real Arduino.h drags the C library in transitively, and several files
// under src/model rely on that rather than including <string.h> themselves.
// Matching the behaviour here is what keeps those files unmodified — the
// alternative was editing the firmware's sources to suit the simulator, which
// is backwards.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>

// LVGL's lv_hal_tick.c is C, and lv_conf.h points LV_TICK_CUSTOM_INCLUDE at
// this header — so everything below must parse as C too. C linkage on these
// two is what lets the C tick source call the C++ definitions in sim_main.cpp.
#ifdef __cplusplus
extern "C" {
#endif

uint32_t millis(void);
void     delay(uint32_t ms);

#ifdef __cplusplus
}
#endif

// PSRAM allocation. The desktop has one undifferentiated heap, so the
// distinction the firmware draws between internal RAM and PSRAM collapses to
// plain malloc. Callers only ever check the result for null, which still works.
#define ps_malloc(n) malloc(n)

#ifdef __cplusplus

// Serial is the debug console. On the panel it is a UART; here it is stdout,
// which is what makes clog/dbg_printf output visible in the terminal you
// launched the simulator from.
struct SimSerial {
    void begin(unsigned long) {}
    void write(uint8_t c) { fputc(c, stdout); }
    size_t write(const uint8_t* b, size_t n) { return fwrite(b, 1, n, stdout); }
    // The IMU recorder paces its console output against this so it never
    // blocks the loop; stdout has no such limit, so say "plenty".
    int availableForWrite() { return 4096; }
    int available() { return 0; }
    int read() { return -1; }
    void print(const char* s) { fputs(s, stdout); }
    void println(const char* s) { fputs(s, stdout); fputc('\n', stdout); }
    void println() { fputc('\n', stdout); }
    void printf(const char* fmt, ...) {
        va_list va;
        va_start(va, fmt);
        vfprintf(stdout, fmt, va);
        va_end(va);
    }
};
extern SimSerial Serial;

#endif  // __cplusplus
