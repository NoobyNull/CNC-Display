#pragma once
// The whole Arduino surface the portable half of this firmware touches.
//
// Verified by grep over src/ui, src/model and src/transport/xmodem_*: millis(),
// delay(), ps_malloc() and Serial's print/println/printf/write. That is all.
// Providing these here is what lets the real UI and model sources compile for
// the desktop simulator without a single #ifdef in them.
//
// src/lv_conf.h names this header in LV_TICK_CUSTOM_INCLUDE, so LVGL's own C
// sources include it too -- everything above the __cplusplus guard must stay
// valid C.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static inline uint32_t micros(void) {
    struct timespec ts;
    // MONOTONIC, not REALTIME: the firmware compares millis() values with
    // wrapping subtraction, and an NTP step backwards would look like a
    // 49-day jump to every one of those comparisons.
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull);
}

static inline uint32_t millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull);
}

static inline void delay(uint32_t ms) { usleep(ms * 1000); }
static inline void delayMicroseconds(uint32_t us) { usleep(us); }

// arduino-esp32's PSRAM allocator. gcode_path.cpp and view_expanded.cpp put
// their big buffers here; on the desktop there is one flat heap.
static inline void* ps_malloc(size_t n) { return malloc(n); }
static inline void* ps_calloc(size_t n, size_t sz) { return calloc(n, sz); }
static inline void* ps_realloc(void* p, size_t n) { return realloc(p, n); }

#ifdef __cplusplus

struct SimSerial {
    void begin(unsigned long = 0) {}
    void end() {}
    operator bool() const { return true; }
    size_t write(uint8_t c) { fputc(c, stdout); return 1; }
    size_t write(const uint8_t* b, size_t n) { return fwrite(b, 1, n, stdout); }
    int    availableForWrite() { return 4096; }      // stdout never pushes back
    int    available() { return 0; }                 // no console input on the bench
    int    read() { return -1; }
    size_t print(const char* s) { fputs(s ? s : "", stdout); return 1; }
    size_t print(int v) { return (size_t)::printf("%d", v); }
    size_t print(unsigned v) { return (size_t)::printf("%u", v); }
    size_t println() { fputc('\n', stdout); return 1; }
    size_t println(const char* s) { ::printf("%s\n", s ? s : ""); return 1; }
    size_t printf(const char* fmt, ...) {
        va_list va;
        va_start(va, fmt);
        int n = vfprintf(stdout, fmt, va);
        va_end(va);
        fflush(stdout);
        return n < 0 ? 0 : (size_t)n;
    }
    void flush() { fflush(stdout); }
};

extern SimSerial Serial;

#endif  // __cplusplus
