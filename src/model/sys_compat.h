#pragma once
// Minimal stand-in for FluidDial's System.h. FluidNCModel.cpp uses four things
// from it — debug output and a delay — and nothing else. Pulling in the whole
// dial-oriented System.h to get them would drag in LovyanGFX, the encoder and
// the M5 stack.

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

inline void dbg_write(uint8_t c) { Serial.write(c); }

inline void dbg_print(const char* s) { Serial.print(s); }

inline void dbg_println(const char* s) { Serial.println(s); }

inline void dbg_printf(const char* fmt, ...) {
    char    buf[192];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    Serial.print(buf);
}

inline void delay_ms(uint32_t ms) { delay(ms); }
