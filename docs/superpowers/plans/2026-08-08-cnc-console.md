# CNC Console Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a FluidNC control console on an Elecrow CrowPanel Advance 7.0" (ESP32-S3, 800×480, GT911 touch), talking to FluidNC over UART2 at 1 Mbaud.

**Architecture:** Copy the proven panel bring-up from `Elcrow-Display-hotkeys`. Port FluidDial's `FluidNCModel` and `FileParser` with their eight `current_scene->` UI callbacks replaced by dirty flags. Build a new LVGL 8.3 UI: a persistent shell (nav rail / DRO bar / content / safety bar) with eight swappable pages plus a full-screen expanded view that flips between toolpath and live console. Single task, no RTOS plumbing.

**Tech Stack:** PlatformIO, Arduino-ESP32, LVGL 8.3.11, LovyanGFX 1.1.8, GrblParserC, json-streaming-parser.

**Spec:** `docs/superpowers/specs/2026-08-08-cnc-console-design.md`

---

## Hardware verification boundary

Tasks marked **[HW]** cannot be verified without the physical board wired to a FluidNC
controller. Their exit criteria are written for a human with the hardware in front of them.
Everything else is verified by `pio run` (compiles) or `pio test -e native` (logic passes).

Wiring required before any **[HW]** task:

| CrowPanel | FluidNC controller |
|---|---|
| GPIO 17 (TX) | pendant UART RX |
| GPIO 18 (RX) | pendant UART TX |
| GND | GND |

---

## File structure

```
platformio.ini              two envs: crowpanel (target), native (host tests)
partitions_4mb.csv          3 MB app / 960 KB spiffs
src/
  main.cpp                  setup + loop
  hw/display_hw.cpp/.h      panel + LVGL init      (copied, unmodified)
  hw/touch.cpp/.h           GT911                  (copied, unmodified)
  lv_conf.h                 LVGL config            (copied, LV_FONT_MONTSERRAT_* enabled)
  transport/uart.cpp/.h     fnc_putchar/fnc_getchar on UART2
  transport/xmodem_rx.cpp/.h  XModem-1K receiver state machine
  model/FluidNCModel.cpp/.h ported from FluidDial
  model/FileParser.cpp/.h   ported from FluidDial
  model/offsets.cpp/.h      parses $# output
  model/gcode_path.cpp/.h   G-code -> point buffer
  model/console_log.cpp/.h  ring buffer + status-frame filter
  ui/theme.h                U, TOUCH_HOLD, TOUCH_TAP, colour tokens
  ui/shell.cpp/.h           grid, nav rail, DRO bar, safety bar
  ui/pages.h                shared page interface
  ui/page_jog.cpp  page_home.cpp  page_probe.cpp  page_offsets.cpp
  ui/page_run.cpp  page_macro.cpp page_diag.cpp   page_setup.cpp
  ui/view_expanded.cpp      toolpath <-> console
  ui/widgets.cpp/.h         shared button/chip/stepper builders
lib/PCA9557/                copied from Elcrow-Display-hotkeys
test/
  test_gcode_path/main.cpp
  test_offsets/main.cpp
  test_xmodem/main.cpp
  test_console_log/main.cpp
```

**Why these four get tests:** they are pure logic with no hardware dependency, and each is
complex enough to fail silently. Everything else is LVGL widget construction, which a unit test
would only assert tautologies about.

---

## Task 1: Project skeleton and build config

**Files:**
- Create: `platformio.ini`, `partitions_4mb.csv`, `src/main.cpp`

- [ ] **Step 1: Write `platformio.ini`**

```ini
[platformio]
default_envs = crowpanel

[env:crowpanel]
platform = espressif32@6.9.0
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi
board_build.flash_mode = qio
board_build.psram_type = opi
board_upload.flash_size = 4MB
board_upload.maximum_size = 4194304
board_build.partitions = partitions_4mb.csv
monitor_speed = 115200
upload_speed = 921600
monitor_filters = esp32_exception_decoder
build_src_filter = +<*> -<.git/>
lib_deps =
    lovyan03/LovyanGFX@^1.1.8
    https://github.com/lvgl/lvgl.git#v8.3.11
    https://github.com/MitchBradley/json-streaming-parser#charp-1.0.2
    https://github.com/MitchBradley/GrblParser#9108f54a3e30526b66194898676af34e96a87da9
build_flags =
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=1
    -DLV_CONF_INCLUDE_SIMPLE
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=0
    -DJSP_USE_CHARP
    -DE4_POS_T
    -DVERBATIM_GCODE_MODES
    -DFNC_BAUD=1000000
    -I src
    -I src/hw

[env:native]
platform = native
build_src_filter = +<model/gcode_path.cpp> +<model/offsets.cpp> +<model/console_log.cpp> +<transport/xmodem_rx.cpp>
build_flags =
    -std=gnu++17
    -DUNIT_TEST
    -I src
test_framework = unity
```

- [ ] **Step 2: Write `partitions_4mb.csv`**

```csv
# Name,   Type, SubType, Offset,  Size,    Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x300000,
spiffs,   data, spiffs,  0x310000,0xF0000,
```

- [ ] **Step 3: Write a stub `src/main.cpp` so the build has an entry point**

```cpp
#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== CNC Console ===");
}

void loop() {
    delay(1000);
}
```

- [ ] **Step 4: Verify it builds**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`. First run downloads LVGL, LovyanGFX, GrblParser — allow several minutes.

- [ ] **Step 5: Commit**

```bash
git add platformio.ini partitions_4mb.csv src/main.cpp
git commit -m "build: PlatformIO skeleton for CrowPanel Advance 7.0"
```

---

## Task 2: Copy the panel bring-up

The bring-up in `Elcrow-Display-hotkeys` is known-good on this exact board. Copy it unmodified;
do not improve it.

**Files:**
- Create: `src/hw/display_hw.cpp`, `src/hw/display_hw.h`, `src/hw/touch.cpp`, `src/hw/touch.h`,
  `src/lv_conf.h`, `lib/PCA9557/*`

- [ ] **Step 1: Copy the files**

```bash
SRC=/home/matthew/Projects/Elcrow-Display-hotkeys
mkdir -p src/hw lib
cp $SRC/display/display_hw.cpp $SRC/display/display_hw.h src/hw/
cp $SRC/display/touch.cpp      $SRC/display/touch.h      src/hw/
cp $SRC/src/lv_conf.h                                    src/
cp -r $SRC/lib/PCA9557                                   lib/
```

- [ ] **Step 2: Enable the fonts the UI needs in `src/lv_conf.h`**

The DRO uses 40 px numerals and labels run down to 10 px. Ensure these lines read `1`:

```c
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1
```

Also confirm `LV_COLOR_DEPTH 16` and `LV_MEM_SIZE` at least `(48U * 1024U)`.

- [ ] **Step 3: Verify it builds**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`

- [ ] **Step 4: Commit**

```bash
git add src/hw src/lv_conf.h lib/PCA9557
git commit -m "hw: copy proven CrowPanel bring-up from Elcrow-Display-hotkeys"
```

---

## Task 3: UART transport

**Files:**
- Create: `src/transport/uart.h`, `src/transport/uart.cpp`

- [ ] **Step 1: Write `src/transport/uart.h`**

```cpp
#pragma once
#include <stdint.h>

// FluidNC link: UART2, GPIO17 TX, GPIO18 RX, 1 Mbaud.
// GPIO 43/44 are the USB console and must not be reused.
#define FNC_UART_NUM 2
#define FNC_TX_PIN   17
#define FNC_RX_PIN   18

void init_fnc_uart();
void reinit_fnc_uart();

extern "C" {
void fnc_putchar(uint8_t c);
int  fnc_getchar();
bool fnc_rx_waiting();
}
```

- [ ] **Step 2: Write `src/transport/uart.cpp`**

Lifted from `FluidDial-main/src/SystemArduino.cpp:133`. `UART_SCLK_XTAL` is what keeps
1 Mbaud stable on the S3 when the APB clock scales — do not change it to the default.

```cpp
#include "uart.h"
#include <Arduino.h>
#include <driver/uart.h>

static uart_port_t fnc_uart_port = (uart_port_t)FNC_UART_NUM;

void init_fnc_uart() {
    fnc_uart_port = (uart_port_t)FNC_UART_NUM;
    uart_driver_delete(fnc_uart_port);
    uart_set_pin(fnc_uart_port, (gpio_num_t)FNC_TX_PIN, (gpio_num_t)FNC_RX_PIN, -1, -1);

    uart_config_t conf = {};
    conf.baud_rate  = FNC_BAUD;
    conf.data_bits  = UART_DATA_8_BITS;
    conf.parity     = UART_PARITY_DISABLE;
    conf.stop_bits  = UART_STOP_BITS_1;
    conf.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    conf.source_clk = UART_SCLK_XTAL;   // independent of APB frequency

    uart_param_config(fnc_uart_port, &conf);
    uart_driver_install(fnc_uart_port, 2048, 0, 0, NULL, 0);
}

void reinit_fnc_uart() { init_fnc_uart(); }

extern "C" void fnc_putchar(uint8_t c) {
    uart_write_bytes(fnc_uart_port, (const char*)&c, 1);
}

extern "C" int fnc_getchar() {
    uint8_t c;
    int n = uart_read_bytes(fnc_uart_port, &c, 1, 0);
    return n == 1 ? c : -1;
}

extern "C" bool fnc_rx_waiting() {
    size_t avail = 0;
    uart_get_buffered_data_len(fnc_uart_port, &avail);
    return avail > 0;
}
```

- [ ] **Step 3: Verify it builds**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`

- [ ] **Step 4: Commit**

```bash
git add src/transport/uart.h src/transport/uart.cpp
git commit -m "transport: UART2 at 1 Mbaud on GPIO 17/18"
```

---

## Task 4: G-code path parser (TDD)

Pure logic, no hardware. This is the piece most likely to fail silently, so it is written
test-first.

**Files:**
- Create: `src/model/gcode_path.h`, `src/model/gcode_path.cpp`
- Test: `test/test_gcode_path/main.cpp`

- [ ] **Step 1: Write the failing test**

`test/test_gcode_path/main.cpp`:

```cpp
#include <unity.h>
#include "model/gcode_path.h"

void setUp() { gpath_reset(); }
void tearDown() {}

static void feed(const char* s) { gpath_feed_line(s); }

void test_absolute_moves_record_points() {
    feed("G21 G90");
    feed("G0 X10 Y20");
    feed("G1 X30 Y40");
    TEST_ASSERT_EQUAL_UINT32(2, gpath_count());
    TEST_ASSERT_EQUAL_FLOAT(10.0f, gpath_point(0).x);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, gpath_point(0).y);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, gpath_point(1).x);
}

void test_relative_moves_accumulate() {
    feed("G21 G91");
    feed("G1 X10");
    feed("G1 X10");
    TEST_ASSERT_EQUAL_UINT32(2, gpath_count());
    TEST_ASSERT_EQUAL_FLOAT(20.0f, gpath_point(1).x);
}

void test_inches_convert_to_mm() {
    feed("G20 G90");
    feed("G1 X1");
    TEST_ASSERT_EQUAL_FLOAT(25.4f, gpath_point(0).x);
}

void test_modal_motion_carries_over() {
    feed("G21 G90 G1");
    feed("X5");
    feed("X10");
    TEST_ASSERT_EQUAL_UINT32(2, gpath_count());
    TEST_ASSERT_EQUAL_FLOAT(10.0f, gpath_point(1).x);
}

void test_rapid_flagged_separately_from_feed() {
    feed("G21 G90");
    feed("G0 X5");
    feed("G1 X10");
    TEST_ASSERT_TRUE(gpath_point(0).rapid);
    TEST_ASSERT_FALSE(gpath_point(1).rapid);
}

void test_comments_and_blank_lines_ignored() {
    feed("G21 G90");
    feed("(this is a comment)");
    feed("; also a comment");
    feed("");
    feed("G1 X7");
    TEST_ASSERT_EQUAL_UINT32(1, gpath_count());
    TEST_ASSERT_EQUAL_FLOAT(7.0f, gpath_point(0).x);
}

void test_arc_is_interpolated_into_segments() {
    feed("G21 G90 G17");
    feed("G0 X0 Y0");
    feed("G2 X10 Y0 I5 J0");
    // one rapid + at least 4 arc segments
    TEST_ASSERT_GREATER_THAN_UINT32(4, gpath_count());
    gpoint_t last = gpath_point(gpath_count() - 1);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 10.0f, last.x);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, last.y);
}

void test_extents_track_min_and_max() {
    feed("G21 G90");
    feed("G1 X-5 Y3");
    feed("G1 X15 Y-2");
    gextents_t e = gpath_extents();
    TEST_ASSERT_EQUAL_FLOAT(-5.0f, e.min_x);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, e.max_x);
    TEST_ASSERT_EQUAL_FLOAT(-2.0f, e.min_y);
    TEST_ASSERT_EQUAL_FLOAT(3.0f,  e.max_y);
}

void test_malformed_words_do_not_crash_or_record() {
    feed("G21 G90");
    feed("G1 X");
    feed("XYZZY");
    feed("G1 Xabc");
    TEST_ASSERT_EQUAL_UINT32(0, gpath_count());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_absolute_moves_record_points);
    RUN_TEST(test_relative_moves_accumulate);
    RUN_TEST(test_inches_convert_to_mm);
    RUN_TEST(test_modal_motion_carries_over);
    RUN_TEST(test_rapid_flagged_separately_from_feed);
    RUN_TEST(test_comments_and_blank_lines_ignored);
    RUN_TEST(test_arc_is_interpolated_into_segments);
    RUN_TEST(test_extents_track_min_and_max);
    RUN_TEST(test_malformed_words_do_not_crash_or_record);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `pio test -e native -f test_gcode_path`
Expected: compile failure — `model/gcode_path.h: No such file or directory`

- [ ] **Step 3: Write `src/model/gcode_path.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

struct gpoint_t {
    float x, y, z;
    bool  rapid;
};

struct gextents_t {
    float min_x, max_x, min_y, max_y, min_z, max_z;
};

// Reset parser state and discard all points.
void gpath_reset();

// Feed one line of G-code. Safe to call with comments, blanks and garbage.
void gpath_feed_line(const char* line);

size_t     gpath_count();
gpoint_t   gpath_point(size_t i);
gextents_t gpath_extents();

// Capacity actually allocated. On device this is PSRAM-backed.
size_t gpath_capacity();
bool   gpath_reserve(size_t points);
```

- [ ] **Step 4: Write `src/model/gcode_path.cpp`**

Modal state machine. Arcs are chorded at ~0.2 mm deviation, clamped to 4..64 segments — enough
that a 5 mm radius arc does not look polygonal, cheap enough that a file full of them does not
exhaust the buffer.

```cpp
#include "model/gcode_path.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#ifndef UNIT_TEST
#  include <Arduino.h>
#  define PATH_ALLOC(n) ((gpoint_t*)ps_malloc((n) * sizeof(gpoint_t)))
#else
#  define PATH_ALLOC(n) ((gpoint_t*)malloc((n) * sizeof(gpoint_t)))
#endif

static gpoint_t*  s_pts   = nullptr;
static size_t     s_cap   = 0;
static size_t     s_count = 0;

static float s_x, s_y, s_z;          // current position, always mm
static bool  s_absolute;             // G90 true / G91 false
static float s_unit;                 // 1.0 mm, 25.4 inches
static int   s_motion;               // 0,1,2,3 = G0,G1,G2,G3
static int   s_plane;                // 17,18,19
static gextents_t s_ext;

static const size_t DEFAULT_CAP = 4096;

size_t gpath_capacity() { return s_cap; }

bool gpath_reserve(size_t points) {
    if (points <= s_cap) return true;
    gpoint_t* n = PATH_ALLOC(points);
    if (!n) return false;
    if (s_pts) {
        memcpy(n, s_pts, s_count * sizeof(gpoint_t));
        free(s_pts);
    }
    s_pts = n;
    s_cap = points;
    return true;
}

void gpath_reset() {
    s_count    = 0;
    s_x = s_y = s_z = 0.0f;
    s_absolute = true;
    s_unit     = 1.0f;
    s_motion   = -1;
    s_plane    = 17;
    s_ext = { 1e9f, -1e9f, 1e9f, -1e9f, 1e9f, -1e9f };
    if (!s_pts) gpath_reserve(DEFAULT_CAP);
}

static void push(float x, float y, float z, bool rapid) {
    if (s_count >= s_cap && !gpath_reserve(s_cap ? s_cap * 2 : DEFAULT_CAP)) return;
    s_pts[s_count++] = { x, y, z, rapid };
    if (x < s_ext.min_x) s_ext.min_x = x;
    if (x > s_ext.max_x) s_ext.max_x = x;
    if (y < s_ext.min_y) s_ext.min_y = y;
    if (y > s_ext.max_y) s_ext.max_y = y;
    if (z < s_ext.min_z) s_ext.min_z = z;
    if (z > s_ext.max_z) s_ext.max_z = z;
}

// Find a word letter and parse its number. Returns false if absent or malformed.
static bool word(const char* s, char letter, float* out) {
    for (const char* p = s; *p; ++p) {
        if (toupper((unsigned char)*p) != letter) continue;
        const char* nptr = p + 1;
        char* end = nullptr;
        float v = strtof(nptr, &end);
        if (end == nptr) return false;      // "X" with no number, or "Xabc"
        *out = v;
        return true;
    }
    return false;
}

static void arc(float ex, float ey, float i, float j, bool cw) {
    float cx = s_x + i, cy = s_y + j;
    float r  = sqrtf(i * i + j * j);
    if (r < 1e-6f) { push(ex, ey, s_z, false); return; }

    float a0 = atan2f(s_y - cy, s_x - cx);
    float a1 = atan2f(ey  - cy, ex  - cx);
    float sweep = a1 - a0;
    if (cw) { while (sweep >= 0) sweep -= 2 * (float)M_PI; }
    else    { while (sweep <= 0) sweep += 2 * (float)M_PI; }

    // Chord deviation 0.2mm -> step angle. Clamp so tiny and huge arcs stay sane.
    float step = 2.0f * acosf(fmaxf(-1.0f, 1.0f - 0.2f / r));
    int n = (int)ceilf(fabsf(sweep) / fmaxf(step, 1e-3f));
    if (n < 4)  n = 4;
    if (n > 64) n = 64;

    for (int k = 1; k <= n; k++) {
        float a = a0 + sweep * ((float)k / (float)n);
        push(cx + r * cosf(a), cy + r * sinf(a), s_z, false);
    }
}

void gpath_feed_line(const char* line) {
    if (!line || !*line) return;
    if (!s_pts) gpath_reset();

    // Strip comments: ( ... ) and ; to end of line
    char buf[256];
    size_t o = 0;
    int paren = 0;
    for (const char* p = line; *p && o < sizeof(buf) - 1; ++p) {
        if (*p == '(') { paren++; continue; }
        if (*p == ')') { if (paren) paren--; continue; }
        if (paren) continue;
        if (*p == ';') break;
        buf[o++] = *p;
    }
    buf[o] = 0;
    if (o == 0) return;

    // Modal words. Multiple G words per line are legal (e.g. "G21 G90 G1").
    for (const char* p = buf; *p; ++p) {
        if (toupper((unsigned char)*p) != 'G') continue;
        char* end = nullptr;
        long g = strtol(p + 1, &end, 10);
        if (end == p + 1) continue;
        switch (g) {
            case 0: case 1: case 2: case 3: s_motion = (int)g; break;
            case 17: case 18: case 19: s_plane = (int)g; break;
            case 20: s_unit = 25.4f; break;
            case 21: s_unit = 1.0f;  break;
            case 90: s_absolute = true;  break;
            case 91: s_absolute = false; break;
            default: break;
        }
    }

    float vx, vy, vz, vi, vj;
    bool hx = word(buf, 'X', &vx);
    bool hy = word(buf, 'Y', &vy);
    bool hz = word(buf, 'Z', &vz);
    if (!hx && !hy && !hz) return;          // no motion on this line
    if (s_motion < 0) return;               // no modal motion established yet

    float nx = s_x, ny = s_y, nz = s_z;
    if (hx) nx = s_absolute ? vx * s_unit : s_x + vx * s_unit;
    if (hy) ny = s_absolute ? vy * s_unit : s_y + vy * s_unit;
    if (hz) nz = s_absolute ? vz * s_unit : s_z + vz * s_unit;

    if (s_motion == 2 || s_motion == 3) {
        vi = vj = 0.0f;
        word(buf, 'I', &vi);
        word(buf, 'J', &vj);
        s_z = nz;
        arc(nx, ny, vi * s_unit, vj * s_unit, s_motion == 2);
    } else {
        push(nx, ny, nz, s_motion == 0);
    }
    s_x = nx; s_y = ny; s_z = nz;
}

size_t   gpath_count()      { return s_count; }
gpoint_t gpath_point(size_t i) { return (i < s_count) ? s_pts[i] : gpoint_t{ 0, 0, 0, false }; }

gextents_t gpath_extents() {
    if (s_count == 0) return { 0, 0, 0, 0, 0, 0 };
    return s_ext;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `pio test -e native -f test_gcode_path`
Expected: `9 Tests 0 Failures 0 Ignored / OK`

- [ ] **Step 6: Commit**

```bash
git add src/model/gcode_path.h src/model/gcode_path.cpp test/test_gcode_path
git commit -m "model: G-code path parser with modal state and arc interpolation"
```

---

## Task 5: Work offset parser (TDD)

FluidNC answers `$#` with lines like `[G54:-100.000,-200.000,-50.000]` and `[TLO:3.175]`.

**Files:**
- Create: `src/model/offsets.h`, `src/model/offsets.cpp`
- Test: `test/test_offsets/main.cpp`

- [ ] **Step 1: Write the failing test**

`test/test_offsets/main.cpp`:

```cpp
#include <unity.h>
#include "model/offsets.h"
#include <string.h>

void setUp() { offsets_reset(); }
void tearDown() {}

void test_parses_a_work_coordinate_system() {
    offsets_feed_line("[G54:-100.000,-200.000,-50.000]");
    const wcs_t* w = offsets_get("G54");
    TEST_ASSERT_NOT_NULL(w);
    TEST_ASSERT_TRUE(w->valid);
    TEST_ASSERT_EQUAL_FLOAT(-100.0f, w->axis[0]);
    TEST_ASSERT_EQUAL_FLOAT(-200.0f, w->axis[1]);
    TEST_ASSERT_EQUAL_FLOAT(-50.0f,  w->axis[2]);
}

void test_parses_all_six_systems() {
    offsets_feed_line("[G54:1.000,0.000,0.000]");
    offsets_feed_line("[G55:2.000,0.000,0.000]");
    offsets_feed_line("[G56:3.000,0.000,0.000]");
    offsets_feed_line("[G57:4.000,0.000,0.000]");
    offsets_feed_line("[G58:5.000,0.000,0.000]");
    offsets_feed_line("[G59:6.000,0.000,0.000]");
    TEST_ASSERT_EQUAL_FLOAT(6.0f, offsets_get("G59")->axis[0]);
}

void test_parses_g28_g30_and_g92() {
    offsets_feed_line("[G28:-2.000,-2.000,-2.000]");
    offsets_feed_line("[G30:0.000,0.000,0.000]");
    offsets_feed_line("[G92:1.500,0.000,0.000]");
    TEST_ASSERT_EQUAL_FLOAT(-2.0f, offsets_get("G28")->axis[0]);
    TEST_ASSERT_EQUAL_FLOAT(1.5f,  offsets_get("G92")->axis[0]);
}

void test_parses_tool_length_offset_as_z_only() {
    offsets_feed_line("[TLO:3.175]");
    const wcs_t* w = offsets_get("TLO");
    TEST_ASSERT_NOT_NULL(w);
    TEST_ASSERT_TRUE(w->valid);
    TEST_ASSERT_EQUAL_FLOAT(3.175f, w->axis[2]);
}

void test_handles_six_axis_machines() {
    offsets_feed_line("[G54:1.0,2.0,3.0,4.0,5.0,6.0]");
    TEST_ASSERT_EQUAL_FLOAT(6.0f, offsets_get("G54")->axis[5]);
}

void test_unknown_and_malformed_lines_are_ignored() {
    offsets_feed_line("[PRB:1.0,2.0,3.0:1]");
    offsets_feed_line("ok");
    offsets_feed_line("[G54:garbage]");
    offsets_feed_line("");
    TEST_ASSERT_FALSE(offsets_get("G54")->valid);
}

void test_unset_system_reports_invalid() {
    TEST_ASSERT_FALSE(offsets_get("G57")->valid);
}

void test_lookup_of_a_nonexistent_name_is_null() {
    TEST_ASSERT_NULL(offsets_get("G99"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parses_a_work_coordinate_system);
    RUN_TEST(test_parses_all_six_systems);
    RUN_TEST(test_parses_g28_g30_and_g92);
    RUN_TEST(test_parses_tool_length_offset_as_z_only);
    RUN_TEST(test_handles_six_axis_machines);
    RUN_TEST(test_unknown_and_malformed_lines_are_ignored);
    RUN_TEST(test_unset_system_reports_invalid);
    RUN_TEST(test_lookup_of_a_nonexistent_name_is_null);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `pio test -e native -f test_offsets`
Expected: compile failure — `model/offsets.h: No such file or directory`

- [ ] **Step 3: Write `src/model/offsets.h`**

```cpp
#pragma once
#include <stdint.h>

#define WCS_MAX_AXES 6

struct wcs_t {
    char  name[8];
    float axis[WCS_MAX_AXES];
    bool  valid;
};

// Rows in display order: G54..G59, G28, G30, G92, TLO
#define WCS_ROW_COUNT 11

void          offsets_reset();
void          offsets_feed_line(const char* line);
const wcs_t*  offsets_get(const char* name);
const wcs_t*  offsets_row(int i);          // i in [0, WCS_ROW_COUNT)
bool          offsets_dirty_take();        // true once after any change
```

- [ ] **Step 4: Write `src/model/offsets.cpp`**

```cpp
#include "model/offsets.h"
#include <string.h>
#include <stdlib.h>

static const char* ROW_NAMES[WCS_ROW_COUNT] = {
    "G54", "G55", "G56", "G57", "G58", "G59", "G28", "G30", "G92", "TLO", "PRB"
};

static wcs_t s_rows[WCS_ROW_COUNT];
static bool  s_dirty = false;

void offsets_reset() {
    for (int i = 0; i < WCS_ROW_COUNT; i++) {
        memset(&s_rows[i], 0, sizeof(wcs_t));
        strncpy(s_rows[i].name, ROW_NAMES[i], sizeof(s_rows[i].name) - 1);
        s_rows[i].valid = false;
    }
    s_dirty = true;
}

static wcs_t* find(const char* name, size_t len) {
    for (int i = 0; i < WCS_ROW_COUNT; i++) {
        if (strlen(s_rows[i].name) == len && strncmp(s_rows[i].name, name, len) == 0) {
            return &s_rows[i];
        }
    }
    return nullptr;
}

void offsets_feed_line(const char* line) {
    if (!line || line[0] != '[') return;
    const char* colon = strchr(line, ':');
    if (!colon) return;

    wcs_t* row = find(line + 1, (size_t)(colon - line - 1));
    if (!row) return;

    float vals[WCS_MAX_AXES] = { 0, 0, 0, 0, 0, 0 };
    int   n = 0;
    const char* p = colon + 1;
    while (n < WCS_MAX_AXES && *p && *p != ']') {
        char* end = nullptr;
        float v = strtof(p, &end);
        if (end == p) return;               // malformed: keep prior state
        vals[n++] = v;
        p = end;
        if (*p == ',') { p++; continue; }
        break;
    }
    if (n == 0) return;

    if (n == 1 && strcmp(row->name, "TLO") == 0) {
        // TLO is a single value and it is the Z offset.
        row->axis[0] = 0.0f;
        row->axis[1] = 0.0f;
        row->axis[2] = vals[0];
    } else {
        for (int i = 0; i < WCS_MAX_AXES; i++) row->axis[i] = (i < n) ? vals[i] : 0.0f;
    }
    row->valid = true;
    s_dirty = true;
}

const wcs_t* offsets_get(const char* name) {
    return find(name, strlen(name));
}

const wcs_t* offsets_row(int i) {
    return (i >= 0 && i < WCS_ROW_COUNT) ? &s_rows[i] : nullptr;
}

bool offsets_dirty_take() {
    bool d = s_dirty;
    s_dirty = false;
    return d;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `pio test -e native -f test_offsets`
Expected: `8 Tests 0 Failures 0 Ignored / OK`

- [ ] **Step 6: Commit**

```bash
git add src/model/offsets.h src/model/offsets.cpp test/test_offsets
git commit -m "model: parse \$# work offset report"
```

---

## Task 6: XModem-1K receiver (TDD)

`$XS=<path>` sends 1024-byte blocks with CRC16. Receiver drives it by emitting `C` to request
CRC mode, then ACK/NAK per block.

**Files:**
- Create: `src/transport/xmodem_rx.h`, `src/transport/xmodem_rx.cpp`
- Test: `test/test_xmodem/main.cpp`

- [ ] **Step 1: Write the failing test**

`test/test_xmodem/main.cpp`:

```cpp
#include <unity.h>
#include "transport/xmodem_rx.h"
#include <string.h>
#include <vector>

static std::vector<uint8_t> g_tx;      // bytes the receiver sent back
static std::vector<uint8_t> g_sink;    // bytes the receiver handed to the consumer

static void tx_cb(uint8_t c) { g_tx.push_back(c); }
static void sink_cb(const uint8_t* d, size_t n) { g_sink.insert(g_sink.end(), d, d + n); }

void setUp() {
    g_tx.clear();
    g_sink.clear();
    xmodem_rx_begin(tx_cb, sink_cb);
}
void tearDown() {}

// CRC16-CCITT, same polynomial FluidNC's xmodem.cpp uses.
static uint16_t crc16(const uint8_t* d, size_t n) {
    uint16_t c = 0;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

static void send_block(uint8_t seq, const uint8_t* payload, size_t size, bool corrupt = false) {
    std::vector<uint8_t> b;
    b.push_back(size == 1024 ? 0x02 : 0x01);   // STX or SOH
    b.push_back(seq);
    b.push_back((uint8_t)~seq);
    b.insert(b.end(), payload, payload + size);
    uint16_t c = crc16(payload, size);
    if (corrupt) c ^= 0xFFFF;
    b.push_back((uint8_t)(c >> 8));
    b.push_back((uint8_t)(c & 0xFF));
    for (uint8_t x : b) xmodem_rx_feed(x);
}

void test_begin_requests_crc_mode() {
    TEST_ASSERT_EQUAL_UINT32(1, g_tx.size());
    TEST_ASSERT_EQUAL_UINT8('C', g_tx[0]);
}

void test_good_1k_block_is_acked_and_delivered() {
    uint8_t p[1024];
    for (int i = 0; i < 1024; i++) p[i] = (uint8_t)(i & 0xFF);
    send_block(1, p, 1024);
    TEST_ASSERT_EQUAL_UINT8(0x06, g_tx.back());          // ACK
    TEST_ASSERT_EQUAL_UINT32(1024, g_sink.size());
    TEST_ASSERT_EQUAL_UINT8(0x00, g_sink[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, g_sink[255]);
}

void test_128_byte_block_is_accepted_too() {
    uint8_t p[128];
    memset(p, 'A', sizeof(p));
    send_block(1, p, 128);
    TEST_ASSERT_EQUAL_UINT8(0x06, g_tx.back());
    TEST_ASSERT_EQUAL_UINT32(128, g_sink.size());
}

void test_bad_crc_is_nakked_and_not_delivered() {
    uint8_t p[128];
    memset(p, 'B', sizeof(p));
    send_block(1, p, 128, /*corrupt=*/true);
    TEST_ASSERT_EQUAL_UINT8(0x15, g_tx.back());          // NAK
    TEST_ASSERT_EQUAL_UINT32(0, g_sink.size());
}

void test_duplicate_block_is_acked_but_not_delivered_twice() {
    uint8_t p[128];
    memset(p, 'C', sizeof(p));
    send_block(1, p, 128);
    send_block(1, p, 128);
    TEST_ASSERT_EQUAL_UINT8(0x06, g_tx.back());
    TEST_ASSERT_EQUAL_UINT32(128, g_sink.size());
}

void test_two_sequential_blocks_deliver_in_order() {
    uint8_t a[128], b[128];
    memset(a, 'a', sizeof(a));
    memset(b, 'b', sizeof(b));
    send_block(1, a, 128);
    send_block(2, b, 128);
    TEST_ASSERT_EQUAL_UINT32(256, g_sink.size());
    TEST_ASSERT_EQUAL_UINT8('a', g_sink[0]);
    TEST_ASSERT_EQUAL_UINT8('b', g_sink[128]);
}

void test_eot_completes_the_transfer() {
    uint8_t p[128];
    memset(p, 'D', sizeof(p));
    send_block(1, p, 128);
    TEST_ASSERT_FALSE(xmodem_rx_done());
    xmodem_rx_feed(0x04);                                 // EOT
    TEST_ASSERT_TRUE(xmodem_rx_done());
    TEST_ASSERT_TRUE(xmodem_rx_ok());
    TEST_ASSERT_EQUAL_UINT8(0x06, g_tx.back());
}

void test_cancel_sequence_aborts() {
    xmodem_rx_feed(0x18);
    xmodem_rx_feed(0x18);
    TEST_ASSERT_TRUE(xmodem_rx_done());
    TEST_ASSERT_FALSE(xmodem_rx_ok());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_begin_requests_crc_mode);
    RUN_TEST(test_good_1k_block_is_acked_and_delivered);
    RUN_TEST(test_128_byte_block_is_accepted_too);
    RUN_TEST(test_bad_crc_is_nakked_and_not_delivered);
    RUN_TEST(test_duplicate_block_is_acked_but_not_delivered_twice);
    RUN_TEST(test_two_sequential_blocks_deliver_in_order);
    RUN_TEST(test_eot_completes_the_transfer);
    RUN_TEST(test_cancel_sequence_aborts);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `pio test -e native -f test_xmodem`
Expected: compile failure — `transport/xmodem_rx.h: No such file or directory`

- [ ] **Step 3: Write `src/transport/xmodem_rx.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef void (*xm_tx_fn)(uint8_t c);
typedef void (*xm_sink_fn)(const uint8_t* data, size_t len);

// Start a receive. Emits 'C' to request CRC mode.
void xmodem_rx_begin(xm_tx_fn tx, xm_sink_fn sink);

// Feed one received byte.
void xmodem_rx_feed(uint8_t c);

bool   xmodem_rx_done();     // EOT seen, or cancelled
bool   xmodem_rx_ok();       // completed successfully
size_t xmodem_rx_bytes();    // payload bytes delivered so far
```

- [ ] **Step 4: Write `src/transport/xmodem_rx.cpp`**

```cpp
#include "transport/xmodem_rx.h"
#include <string.h>

#define SOH 0x01
#define STX 0x02
#define ETX_EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18

enum rx_state_t { RX_IDLE, RX_SEQ, RX_SEQ_INV, RX_DATA, RX_CRC_HI, RX_CRC_LO };

static xm_tx_fn   s_tx;
static xm_sink_fn s_sink;
static rx_state_t s_state;
static uint8_t    s_buf[1024];
static size_t     s_size, s_pos;
static uint8_t    s_seq, s_seq_inv;
static uint8_t    s_expect;
static uint16_t   s_crc;
static bool       s_done, s_ok;
static size_t     s_bytes;
static int        s_cans;

static uint16_t crc16(const uint8_t* d, size_t n) {
    uint16_t c = 0;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

void xmodem_rx_begin(xm_tx_fn tx, xm_sink_fn sink) {
    s_tx = tx; s_sink = sink;
    s_state = RX_IDLE;
    s_size = s_pos = s_bytes = 0;
    s_expect = 1;
    s_done = s_ok = false;
    s_cans = 0;
    if (s_tx) s_tx('C');            // request CRC mode
}

void xmodem_rx_feed(uint8_t c) {
    if (s_done) return;

    if (s_state == RX_IDLE) {
        switch (c) {
            case SOH: s_size = 128;  s_pos = 0; s_state = RX_SEQ; return;
            case STX: s_size = 1024; s_pos = 0; s_state = RX_SEQ; return;
            case ETX_EOT:
                s_done = true; s_ok = true;
                if (s_tx) s_tx(ACK);
                return;
            case CAN:
                if (++s_cans >= 2) { s_done = true; s_ok = false; }
                return;
            default:
                s_cans = 0;
                return;
        }
    }
    s_cans = 0;

    switch (s_state) {
        case RX_SEQ:
            s_seq = c;
            s_state = RX_SEQ_INV;
            return;

        case RX_SEQ_INV:
            s_seq_inv = c;
            s_state = RX_DATA;
            return;

        case RX_DATA:
            s_buf[s_pos++] = c;
            if (s_pos >= s_size) s_state = RX_CRC_HI;
            return;

        case RX_CRC_HI:
            s_crc = (uint16_t)c << 8;
            s_state = RX_CRC_LO;
            return;

        case RX_CRC_LO: {
            s_crc |= c;
            s_state = RX_IDLE;

            bool seq_ok = (uint8_t)(s_seq ^ s_seq_inv) == 0xFF;
            bool crc_ok = crc16(s_buf, s_size) == s_crc;

            if (!seq_ok || !crc_ok) {
                if (s_tx) s_tx(NAK);
                return;
            }
            if (s_seq == s_expect) {
                if (s_sink) s_sink(s_buf, s_size);
                s_bytes += s_size;
                s_expect = (uint8_t)(s_expect + 1);
            }
            // A repeat of the previous block means our ACK was lost; re-ACK, don't re-deliver.
            if (s_tx) s_tx(ACK);
            return;
        }

        default:
            s_state = RX_IDLE;
            return;
    }
}

bool   xmodem_rx_done()  { return s_done; }
bool   xmodem_rx_ok()    { return s_ok; }
size_t xmodem_rx_bytes() { return s_bytes; }
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `pio test -e native -f test_xmodem`
Expected: `8 Tests 0 Failures 0 Ignored / OK`

- [ ] **Step 6: Commit**

```bash
git add src/transport/xmodem_rx.h src/transport/xmodem_rx.cpp test/test_xmodem
git commit -m "transport: XModem-1K receiver for \$XS bulk file pull"
```

---

## Task 7: Console log with status-frame filter (TDD)

FluidNC emits `<Run|MPos:...>` several times a second. Unfiltered, those scroll real errors out
of a 13-line window in seconds. The filter is the point of this module.

**Files:**
- Create: `src/model/console_log.h`, `src/model/console_log.cpp`
- Test: `test/test_console_log/main.cpp`

- [ ] **Step 1: Write the failing test**

`test/test_console_log/main.cpp`:

```cpp
#include <unity.h>
#include "model/console_log.h"
#include <string.h>

void setUp() { clog_reset(); clog_set_show_status(false); }
void tearDown() {}

void test_status_frames_are_hidden_by_default() {
    clog_add("<Idle|MPos:0.000,0.000,0.000|FS:0,0>");
    TEST_ASSERT_EQUAL_UINT32(0, clog_count());
}

void test_status_frames_appear_when_enabled() {
    clog_set_show_status(true);
    clog_add("<Idle|MPos:0.000,0.000,0.000|FS:0,0>");
    TEST_ASSERT_EQUAL_UINT32(1, clog_count());
}

void test_ordinary_lines_are_kept_and_classified() {
    clog_add("ok");
    clog_add("error:9 G-code locked out");
    clog_add("ALARM:1");
    clog_add("[MSG:INFO: Tool change]");
    clog_add("[GC:G1 G54 G90]");
    clog_add("> G1 X10");
    TEST_ASSERT_EQUAL_UINT32(6, clog_count());
    TEST_ASSERT_EQUAL_INT(CLOG_OK,    clog_line(0)->kind);
    TEST_ASSERT_EQUAL_INT(CLOG_ERROR, clog_line(1)->kind);
    TEST_ASSERT_EQUAL_INT(CLOG_ALARM, clog_line(2)->kind);
    TEST_ASSERT_EQUAL_INT(CLOG_MSG,   clog_line(3)->kind);
    TEST_ASSERT_EQUAL_INT(CLOG_INFO,  clog_line(4)->kind);
    TEST_ASSERT_EQUAL_INT(CLOG_SENT,  clog_line(5)->kind);
}

void test_ring_buffer_drops_oldest_when_full() {
    char b[16];
    for (int i = 0; i < CLOG_CAPACITY + 10; i++) {
        snprintf(b, sizeof(b), "line%d", i);
        clog_add(b);
    }
    TEST_ASSERT_EQUAL_UINT32(CLOG_CAPACITY, clog_count());
    TEST_ASSERT_EQUAL_STRING("line10", clog_line(0)->text);
}

void test_long_lines_are_truncated_not_overflowed() {
    char big[512];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    clog_add(big);
    TEST_ASSERT_EQUAL_UINT32(1, clog_count());
    TEST_ASSERT_TRUE(strlen(clog_line(0)->text) < CLOG_LINE_MAX);
}

void test_blank_and_null_input_is_ignored() {
    clog_add("");
    clog_add(nullptr);
    TEST_ASSERT_EQUAL_UINT32(0, clog_count());
}

void test_dirty_flag_is_set_on_add_and_cleared_on_take() {
    clog_add("ok");
    TEST_ASSERT_TRUE(clog_dirty_take());
    TEST_ASSERT_FALSE(clog_dirty_take());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_status_frames_are_hidden_by_default);
    RUN_TEST(test_status_frames_appear_when_enabled);
    RUN_TEST(test_ordinary_lines_are_kept_and_classified);
    RUN_TEST(test_ring_buffer_drops_oldest_when_full);
    RUN_TEST(test_long_lines_are_truncated_not_overflowed);
    RUN_TEST(test_blank_and_null_input_is_ignored);
    RUN_TEST(test_dirty_flag_is_set_on_add_and_cleared_on_take);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `pio test -e native -f test_console_log`
Expected: compile failure — `model/console_log.h: No such file or directory`

- [ ] **Step 3: Write `src/model/console_log.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define CLOG_CAPACITY 120
#define CLOG_LINE_MAX 96

enum clog_kind_t { CLOG_SENT, CLOG_OK, CLOG_ERROR, CLOG_ALARM, CLOG_MSG, CLOG_INFO, CLOG_STATUS };

struct clog_line_t {
    char        text[CLOG_LINE_MAX];
    clog_kind_t kind;
};

void  clog_reset();
void  clog_add(const char* line);
void  clog_set_show_status(bool on);
bool  clog_show_status();

size_t             clog_count();
const clog_line_t* clog_line(size_t i);   // 0 = oldest retained
bool               clog_dirty_take();
```

- [ ] **Step 4: Write `src/model/console_log.cpp`**

```cpp
#include "model/console_log.h"
#include <string.h>

static clog_line_t s_ring[CLOG_CAPACITY];
static size_t s_head  = 0;      // index of oldest
static size_t s_count = 0;
static bool   s_show_status = false;
static bool   s_dirty = false;

void clog_reset() {
    s_head = s_count = 0;
    s_dirty = true;
}

void clog_set_show_status(bool on) { s_show_status = on; s_dirty = true; }
bool clog_show_status()            { return s_show_status; }

static clog_kind_t classify(const char* s) {
    if (s[0] == '<')                          return CLOG_STATUS;
    if (s[0] == '>')                          return CLOG_SENT;
    if (strncmp(s, "ok", 2) == 0)             return CLOG_OK;
    if (strncmp(s, "error:", 6) == 0)         return CLOG_ERROR;
    if (strncmp(s, "ALARM:", 6) == 0)         return CLOG_ALARM;
    if (strncmp(s, "[MSG:", 5) == 0)          return CLOG_MSG;
    if (s[0] == '[')                          return CLOG_INFO;
    return CLOG_INFO;
}

void clog_add(const char* line) {
    if (!line || !*line) return;

    clog_kind_t k = classify(line);
    if (k == CLOG_STATUS && !s_show_status) return;

    size_t slot;
    if (s_count < CLOG_CAPACITY) {
        slot = (s_head + s_count) % CLOG_CAPACITY;
        s_count++;
    } else {
        slot   = s_head;
        s_head = (s_head + 1) % CLOG_CAPACITY;
    }

    strncpy(s_ring[slot].text, line, CLOG_LINE_MAX - 1);
    s_ring[slot].text[CLOG_LINE_MAX - 1] = 0;
    s_ring[slot].kind = k;
    s_dirty = true;
}

size_t clog_count() { return s_count; }

const clog_line_t* clog_line(size_t i) {
    if (i >= s_count) return nullptr;
    return &s_ring[(s_head + i) % CLOG_CAPACITY];
}

bool clog_dirty_take() {
    bool d = s_dirty;
    s_dirty = false;
    return d;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `pio test -e native -f test_console_log`
Expected: `7 Tests 0 Failures 0 Ignored / OK`

- [ ] **Step 6: Commit**

```bash
git add src/model/console_log.h src/model/console_log.cpp test/test_console_log
git commit -m "model: console ring buffer with status-frame filter"
```

---

## Task 8: Port FluidNCModel

**Files:**
- Create: `src/model/FluidNCModel.cpp`, `src/model/FluidNCModel.h`
- Reference: `FluidDial-main/src/FluidNCModel.cpp`, `FluidDial-main/src/FluidNCModel.h`

- [ ] **Step 1: Copy the files**

```bash
mkdir -p src/model
cp FluidDial-main/src/FluidNCModel.cpp FluidDial-main/src/FluidNCModel.h src/model/
```

- [ ] **Step 2: Strip the UI coupling from `src/model/FluidNCModel.cpp`**

Remove these includes near the top:

```cpp
#include "Scene.h"          // DELETE
#include "HomingScene.h"    // DELETE
extern Scene statusScene;   // DELETE
```

Add instead:

```cpp
#include "model/model_events.h"
```

Then replace every `current_scene->` call. There are eight; find them with
`grep -n 'current_scene->' src/model/FluidNCModel.cpp`. The mapping:

| Was | Becomes |
|---|---|
| `current_scene->onDROChange()` | `model_flag_dro()` |
| `current_scene->onStateChange(...)` | `model_flag_state()` |
| `current_scene->onLimitsChange()` | `model_flag_limits()` |
| `current_scene->onError(msg)` | `model_flag_error(msg)` |
| `current_scene->onMessage(c, a)` | `model_flag_message(c, a)` |

Also replace the `set_disconnected_state()` body's scene activation with `model_flag_state()`,
and delete `#include "HomingScene.h"`'s companion call `set_axis_homed(...)` — homing state is
re-derived in Task 12 from the status report instead.

- [ ] **Step 3: Write `src/model/model_events.h`**

```cpp
#pragma once

// The model writes globals then raises a flag. The UI polls the flags on an
// lv_timer. No observer registry: this UI is a fixed screen set, not a plugin host.
void model_flag_dro();
void model_flag_state();
void model_flag_limits();
void model_flag_error(const char* msg);
void model_flag_message(const char* cmd, const char* args);

bool model_take_dro();
bool model_take_state();
bool model_take_limits();
const char* model_take_error();      // nullptr if none pending
```

- [ ] **Step 4: Write `src/model/model_events.cpp`**

```cpp
#include "model/model_events.h"
#include "model/console_log.h"
#include <string.h>
#include <stdio.h>

static bool s_dro = false, s_state = false, s_limits = false;
static char s_error[96] = { 0 };
static bool s_error_pending = false;

void model_flag_dro()    { s_dro = true; }
void model_flag_state()  { s_state = true; }
void model_flag_limits() { s_limits = true; }

void model_flag_error(const char* msg) {
    if (!msg) return;
    strncpy(s_error, msg, sizeof(s_error) - 1);
    s_error[sizeof(s_error) - 1] = 0;
    s_error_pending = true;
    char buf[CLOG_LINE_MAX];
    snprintf(buf, sizeof(buf), "error: %s", msg);
    clog_add(buf);
}

void model_flag_message(const char* cmd, const char* args) {
    char buf[CLOG_LINE_MAX];
    snprintf(buf, sizeof(buf), "[MSG:%s %s]", cmd ? cmd : "", args ? args : "");
    clog_add(buf);
}

bool model_take_dro()    { bool d = s_dro;    s_dro    = false; return d; }
bool model_take_state()  { bool d = s_state;  s_state  = false; return d; }
bool model_take_limits() { bool d = s_limits; s_limits = false; return d; }

const char* model_take_error() {
    if (!s_error_pending) return nullptr;
    s_error_pending = false;
    return s_error;
}
```

- [ ] **Step 5: Verify it builds**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`. If the linker complains about a missing `current_scene`, a call was
missed — run the grep from Step 2 again.

- [ ] **Step 6: Commit**

```bash
git add src/model/FluidNCModel.cpp src/model/FluidNCModel.h src/model/model_events.h src/model/model_events.cpp
git commit -m "model: port FluidNCModel, replacing scene callbacks with dirty flags"
```

---

## Task 9: Theme tokens and shared widgets

**Files:**
- Create: `src/ui/theme.h`, `src/ui/widgets.h`, `src/ui/widgets.cpp`

- [ ] **Step 1: Write `src/ui/theme.h`**

```cpp
#pragma once
#include <lvgl.h>

// Two constants are hand-set because physics sets them. Everything else derives.
static const lv_coord_t U          = 8;        // grid quantum
static const lv_coord_t TOUCH_HOLD = U * 10;   // 80px: keys you hold contact on
static const lv_coord_t TOUCH_TAP  = U * 7;    // 56px floor: discrete taps

static const lv_coord_t SCREEN_W   = 800;
static const lv_coord_t SCREEN_H   = 480;
static const lv_coord_t RAIL_W     = U * 12;   // 96
static const lv_coord_t RAIL_SCROLL_H = U * 5; // 40
static const lv_coord_t DRO_H      = U * 18;   // 144
static const lv_coord_t SAFETY_H   = U * 10;   // 80
static const lv_coord_t CONTENT_W  = SCREEN_W - RAIL_W;   // 704
static const lv_coord_t CONTENT_H  = SCREEN_H - DRO_H - SAFETY_H;  // 256

#define C_BG       lv_color_hex(0x0D1114)
#define C_SURFACE  lv_color_hex(0x161C20)
#define C_RAISE    lv_color_hex(0x1F272C)
#define C_LINE     lv_color_hex(0x2B353B)
#define C_TEXT     lv_color_hex(0xE8EFF2)
#define C_DIM      lv_color_hex(0x7E8F99)
#define C_ACCENT   lv_color_hex(0x3FC1CC)
#define C_OK       lv_color_hex(0x46C25A)
#define C_RUN      lv_color_hex(0x4C93F5)
#define C_WARN     lv_color_hex(0xE0A22C)
#define C_CRIT     lv_color_hex(0xF4574C)
#define C_SEL_BG   lv_color_hex(0x12262A)
```

- [ ] **Step 2: Write `src/ui/widgets.h`**

```cpp
#pragma once
#include <lvgl.h>
#include "ui/theme.h"

// A flat panel: surface fill, hairline border, small radius, no scroll.
lv_obj_t* w_panel(lv_obj_t* parent, lv_color_t bg = C_SURFACE);

// A labelled action button. `sub` may be nullptr; when present it renders small
// and dim beneath the label (used to show the G-code a button will send).
lv_obj_t* w_button(lv_obj_t* parent, const char* label, const char* sub,
                   lv_color_t bg, lv_color_t fg, lv_event_cb_t cb, void* user);

// A label with a given font and colour, no layout of its own.
lv_obj_t* w_label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t col);

// Remove padding, border and scrollbars so a container is pure layout.
void w_flatten(lv_obj_t* o);
```

- [ ] **Step 3: Write `src/ui/widgets.cpp`**

```cpp
#include "ui/widgets.h"

void w_flatten(lv_obj_t* o) {
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_pad_gap(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* w_panel(lv_obj_t* parent, lv_color_t bg) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, C_LINE, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 3, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

lv_obj_t* w_label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t col) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

lv_obj_t* w_button(lv_obj_t* parent, const char* label, const char* sub,
                   lv_color_t bg, lv_color_t fg, lv_event_cb_t cb, void* user) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_border_color(b, C_LINE, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 3, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 2, 0);
    // Pressed state is visible, not just felt.
    lv_obj_set_style_bg_color(b, lv_color_lighten(bg, 40), LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);

    lv_obj_t* col = lv_obj_create(b);
    w_flatten(col);
    lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    w_label(col, label, &lv_font_montserrat_16, fg);
    if (sub) {
        lv_obj_t* s = w_label(col, sub, &lv_font_montserrat_10, C_DIM);
        lv_obj_set_style_text_color(s, lv_color_mix(fg, C_DIM, 128), 0);
    }
    return b;
}
```

- [ ] **Step 4: Verify it builds**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add src/ui/theme.h src/ui/widgets.h src/ui/widgets.cpp
git commit -m "ui: theme tokens and shared widget builders"
```

---

## Task 10: Shell — grid, nav rail, DRO bar, safety bar

**Files:**
- Create: `src/ui/shell.h`, `src/ui/shell.cpp`, `src/ui/pages.h`

- [ ] **Step 1: Write `src/ui/pages.h`**

```cpp
#pragma once
#include <lvgl.h>

// Every page builds into the supplied 704x256 container and returns nothing.
// Pages are built once at startup and shown/hidden, not created and destroyed:
// rebuilding an LVGL tree on every nav tap is a needless allocation churn.
typedef void (*page_build_fn)(lv_obj_t* parent);

enum page_id_t {
    PAGE_JOG = 0, PAGE_HOME, PAGE_PROBE, PAGE_OFFSETS,
    PAGE_RUN, PAGE_MACRO, PAGE_DIAG, PAGE_SETUP,
    PAGE_COUNT
};

void page_jog_build(lv_obj_t* p);
void page_home_build(lv_obj_t* p);
void page_probe_build(lv_obj_t* p);
void page_offsets_build(lv_obj_t* p);
void page_run_build(lv_obj_t* p);
void page_macro_build(lv_obj_t* p);
void page_diag_build(lv_obj_t* p);
void page_setup_build(lv_obj_t* p);

// Called on the shared 20 Hz tick while a page is visible.
void page_jog_tick();
void page_offsets_tick();
void page_run_tick();
void page_diag_tick();
```

- [ ] **Step 2: Write `src/ui/shell.h`**

```cpp
#pragma once
#include <lvgl.h>
#include "ui/pages.h"

void shell_build();
void shell_show_page(page_id_t id);
page_id_t shell_current_page();

// Full-screen expanded view: hides rail and DRO, keeps the safety bar.
void shell_set_expanded(bool on);
bool shell_expanded();

// Called at 20 Hz from the LVGL timer.
void shell_tick();
```

- [ ] **Step 3: Write `src/ui/shell.cpp`**

```cpp
#include "ui/shell.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "model/model_events.h"
#include "model/offsets.h"
#include "GrblParserC.h"
#include <stdio.h>

// ---- nav rail: 8 destinations, a 5-item window, arrows top and bottom ----
#define NAV_COUNT   8
#define NAV_VISIBLE 5

static const char* NAV_LABEL[NAV_COUNT] = {
    "JOG", "HOME", "PROBE", "OFFSET", "RUN", "MACRO", "DIAG", "SETUP"
};

static lv_obj_t* s_scr;
static lv_obj_t* s_rail;
static lv_obj_t* s_rail_up;
static lv_obj_t* s_rail_down;
static lv_obj_t* s_nav_btn[NAV_VISIBLE];
static lv_obj_t* s_nav_lbl[NAV_VISIBLE];
static lv_obj_t* s_dro;
static lv_obj_t* s_content;
static lv_obj_t* s_safety;
static lv_obj_t* s_pages[PAGE_COUNT];

static lv_obj_t* s_state_chip;
static lv_obj_t* s_state_lbl;
static lv_obj_t* s_wcs_lbl;
static lv_obj_t* s_link_lbl;
static lv_obj_t* s_axis_work[3];
static lv_obj_t* s_axis_mach[3];

static int       s_nav_start = 0;
static page_id_t s_page = PAGE_JOG;
static bool      s_expanded = false;

page_id_t shell_current_page() { return s_page; }
bool      shell_expanded()     { return s_expanded; }

// ---- state chip colours: colour AND word, never colour alone ----
static lv_color_t state_colour(state_t s) {
    switch (s) {
        case Idle:                       return C_OK;
        case Cycle: case Homing:         return C_RUN;
        case Jog:                        return C_ACCENT;
        case Hold: case DoorOpen:
        case DoorClosed:                 return C_WARN;
        case Alarm: case Critical:
        case ConfigAlarm:                return C_CRIT;
        case Disconnected:               return C_LINE;
        default:                         return C_DIM;
    }
}

static const char* state_word(state_t s) {
    switch (s) {
        case Idle:        return "IDLE";
        case Cycle:       return "RUN";
        case Jog:         return "JOG";
        case Homing:      return "HOME";
        case Hold:        return "HOLD";
        case DoorOpen:
        case DoorClosed:  return "DOOR";
        case Alarm:       return "ALARM";
        case Critical:    return "CRIT";
        case ConfigAlarm: return "CONFIG";
        case GrblSleep:   return "SLEEP";
        case CheckMode:   return "CHECK";
        case Disconnected:return "NO LINK";
        default:          return "?";
    }
}

// ---- nav ----
static void nav_refresh() {
    for (int i = 0; i < NAV_VISIBLE; i++) {
        int idx = s_nav_start + i;
        lv_label_set_text(s_nav_lbl[i], NAV_LABEL[idx]);
        bool on = (idx == (int)s_page);
        lv_obj_set_style_bg_color(s_nav_btn[i], on ? C_ACCENT : C_SURFACE, 0);
        lv_obj_set_style_text_color(s_nav_lbl[i], on ? C_BG : C_DIM, 0);
    }
    lv_obj_set_style_text_color(s_rail_up,   s_nav_start > 0 ? C_DIM : C_LINE, 0);
    lv_obj_set_style_text_color(s_rail_down, s_nav_start < NAV_COUNT - NAV_VISIBLE ? C_DIM : C_LINE, 0);
}

static void nav_scroll_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int n = s_nav_start + dir;
    if (n < 0) n = 0;
    if (n > NAV_COUNT - NAV_VISIBLE) n = NAV_COUNT - NAV_VISIBLE;
    s_nav_start = n;
    nav_refresh();
}

static void nav_pick_cb(lv_event_t* e) {
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    shell_show_page((page_id_t)(s_nav_start + slot));
}

void shell_show_page(page_id_t id) {
    if (id < 0 || id >= PAGE_COUNT) return;
    s_page = id;
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (i == (int)id) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else              lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    // Keep the active item on screen when navigated to programmatically.
    if ((int)id < s_nav_start)                    s_nav_start = (int)id;
    if ((int)id > s_nav_start + NAV_VISIBLE - 1)  s_nav_start = (int)id - NAV_VISIBLE + 1;
    nav_refresh();
}

// ---- safety bar: present on every screen, always the same place ----
static void hold_cb(lv_event_t*)   { fnc_realtime(FeedHold); }
static void resume_cb(lv_event_t*) { fnc_realtime(CycleStart); }
static void reset_cb(lv_event_t*)  { fnc_realtime(Reset); }

static void build_safety(lv_obj_t* parent) {
    s_safety = lv_obj_create(parent);
    w_flatten(s_safety);
    lv_obj_set_style_bg_color(s_safety, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(s_safety, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s_safety, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(s_safety, C_LINE, 0);
    lv_obj_set_style_border_width(s_safety, 1, 0);
    lv_obj_set_style_pad_all(s_safety, U, 0);
    lv_obj_set_style_pad_gap(s_safety, U, 0);
    lv_obj_set_flex_flow(s_safety, LV_FLEX_FLOW_ROW);

    struct { const char* t; lv_color_t bg; lv_event_cb_t cb; } B[] = {
        { "HOLD",   C_WARN, hold_cb },
        { "RESUME", C_OK,   resume_cb },
        { "RESET",  C_CRIT, reset_cb },
    };
    for (auto& b : B) {
        lv_obj_t* o = w_button(s_safety, b.t, nullptr, b.bg, C_BG, b.cb, nullptr);
        lv_obj_set_size(o, U * 19, LV_PCT(100));   // 152 x 64
    }
}

// ---- DRO ----
static void build_dro(lv_obj_t* parent) {
    s_dro = lv_obj_create(parent);
    w_flatten(s_dro);
    lv_obj_set_style_border_side(s_dro, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(s_dro, C_LINE, 0);
    lv_obj_set_style_border_width(s_dro, 1, 0);
    lv_obj_set_flex_flow(s_dro, LV_FLEX_FLOW_ROW);

    // state column
    lv_obj_t* sc = lv_obj_create(s_dro);
    w_flatten(sc);
    lv_obj_set_size(sc, U * 16, LV_PCT(100));
    lv_obj_set_style_pad_all(sc, U, 0);
    lv_obj_set_style_pad_gap(sc, 5, 0);
    lv_obj_set_style_border_side(sc, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(sc, C_LINE, 0);
    lv_obj_set_style_border_width(sc, 1, 0);
    lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sc, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_state_chip = w_panel(sc, C_OK);
    lv_obj_set_size(s_state_chip, LV_PCT(100), U * 5);
    lv_obj_set_style_border_width(s_state_chip, 0, 0);
    s_state_lbl = w_label(s_state_chip, "IDLE", &lv_font_montserrat_16, C_BG);
    lv_obj_center(s_state_lbl);

    s_wcs_lbl = w_label(sc, "G54", &lv_font_montserrat_12, C_ACCENT);
    s_link_lbl = w_label(sc, "UART 1M", &lv_font_montserrat_10, C_DIM);

    // three axis cells
    lv_obj_t* axes = lv_obj_create(s_dro);
    w_flatten(axes);
    lv_obj_set_size(axes, CONTENT_W - U * 16, LV_PCT(100));
    lv_obj_set_flex_flow(axes, LV_FLEX_FLOW_ROW);

    static const char* AX[3] = { "X", "Y", "Z" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* c = lv_obj_create(axes);
        w_flatten(c);
        lv_obj_set_size(c, (CONTENT_W - U * 16) / 3, LV_PCT(100));
        lv_obj_set_style_pad_left(c, U * 2, 0);
        lv_obj_set_style_border_side(c, LV_BORDER_SIDE_RIGHT, 0);
        lv_obj_set_style_border_color(c, C_LINE, 0);
        lv_obj_set_style_border_width(c, i < 2 ? 1 : 0, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        w_label(c, AX[i], &lv_font_montserrat_12, C_DIM);
        s_axis_work[i] = w_label(c, "0.000", &lv_font_montserrat_40, C_TEXT);
        s_axis_mach[i] = w_label(c, "M 0.000", &lv_font_montserrat_12, C_DIM);
    }
}

// ---- rail ----
static void build_rail(lv_obj_t* parent) {
    s_rail = lv_obj_create(parent);
    w_flatten(s_rail);
    lv_obj_set_style_bg_color(s_rail, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(s_rail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s_rail, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(s_rail, C_LINE, 0);
    lv_obj_set_style_border_width(s_rail, 1, 0);
    lv_obj_set_flex_flow(s_rail, LV_FLEX_FLOW_COLUMN);

    s_rail_up = w_button(s_rail, LV_SYMBOL_UP, nullptr, C_RAISE, C_DIM, nav_scroll_cb, (void*)(intptr_t)-1);
    lv_obj_set_size(s_rail_up, LV_PCT(100), RAIL_SCROLL_H);

    for (int i = 0; i < NAV_VISIBLE; i++) {
        lv_obj_t* b = lv_btn_create(s_rail);
        lv_obj_set_size(b, LV_PCT(100), TOUCH_HOLD);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, C_SURFACE, 0);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(b, C_LINE, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_add_event_cb(b, nav_pick_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        s_nav_btn[i] = b;
        s_nav_lbl[i] = w_label(b, NAV_LABEL[i], &lv_font_montserrat_12, C_DIM);
        lv_obj_center(s_nav_lbl[i]);
    }

    s_rail_down = w_button(s_rail, LV_SYMBOL_DOWN, nullptr, C_RAISE, C_DIM, nav_scroll_cb, (void*)(intptr_t)1);
    lv_obj_set_size(s_rail_down, LV_PCT(100), RAIL_SCROLL_H);
}

void shell_build() {
    s_scr = lv_scr_act();
    lv_obj_set_style_bg_color(s_scr, C_BG, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_scr, 0, 0);

    build_rail(s_scr);
    lv_obj_set_pos(s_rail, 0, 0);
    lv_obj_set_size(s_rail, RAIL_W, SCREEN_H);

    lv_obj_t* right = lv_obj_create(s_scr);
    w_flatten(right);
    lv_obj_set_pos(right, RAIL_W, 0);
    lv_obj_set_size(right, CONTENT_W, SCREEN_H);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);

    build_dro(right);
    lv_obj_set_size(s_dro, CONTENT_W, DRO_H);

    s_content = lv_obj_create(right);
    w_flatten(s_content);
    lv_obj_set_size(s_content, CONTENT_W, CONTENT_H);
    lv_obj_set_style_pad_all(s_content, U, 0);

    build_safety(right);
    lv_obj_set_size(s_safety, CONTENT_W, SAFETY_H);

    // Pages are built once and hidden, not rebuilt on navigation.
    page_build_fn BUILD[PAGE_COUNT] = {
        page_jog_build, page_home_build, page_probe_build, page_offsets_build,
        page_run_build, page_macro_build, page_diag_build, page_setup_build
    };
    for (int i = 0; i < PAGE_COUNT; i++) {
        s_pages[i] = lv_obj_create(s_content);
        w_flatten(s_pages[i]);
        lv_obj_set_size(s_pages[i], CONTENT_W - U * 2, CONTENT_H - U * 2);
        lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        BUILD[i](s_pages[i]);
    }
    shell_show_page(PAGE_JOG);
}

void shell_set_expanded(bool on) {
    s_expanded = on;
    if (on) {
        lv_obj_add_flag(s_rail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_dro,  LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_rail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_dro,  LV_OBJ_FLAG_HIDDEN);
    }
}

static void fmt_pos(char* out, size_t n, pos_t v) {
    snprintf(out, n, "%.3f", (double)v / 10000.0);   // E4_POS_T: fixed point, 4 decimals
}

void shell_tick() {
    if (model_take_state()) {
        lv_label_set_text(s_state_lbl, state_word(state));
        lv_obj_set_style_bg_color(s_state_chip, state_colour(state), 0);
        lv_obj_set_style_text_color(s_state_lbl,
            state == Disconnected ? C_DIM : C_BG, 0);
    }
    if (model_take_dro()) {
        char b[24];
        int n = n_axes < 3 ? n_axes : 3;
        for (int a = 0; a < n; a++) {
            fmt_pos(b, sizeof(b), myAxes[a]);
            lv_label_set_text(s_axis_work[a], b);
        }
    }
    if (offsets_dirty_take()) {
        lv_label_set_text(s_wcs_lbl, mode_string() ? mode_string() : "G54");
    }
    switch (s_page) {
        case PAGE_JOG:     page_jog_tick();     break;
        case PAGE_OFFSETS: page_offsets_tick(); break;
        case PAGE_RUN:     page_run_tick();     break;
        case PAGE_DIAG:    page_diag_tick();    break;
        default: break;
    }
}
```

- [ ] **Step 4: Verify it builds**

Run: `pio run -e crowpanel`
Expected: link errors for the eight `page_*_build` symbols — that is correct at this point.
Task 11 supplies stubs. If there are *compile* errors inside `shell.cpp`, fix those now.

- [ ] **Step 5: Commit**

```bash
git add src/ui/shell.h src/ui/shell.cpp src/ui/pages.h
git commit -m "ui: persistent shell with scrolling nav rail, DRO bar and safety bar"
```

---

## Task 11: Page stubs so the shell links

Each page gets a real file now and real content in later tasks. This keeps the tree building at
every commit rather than leaving it broken across several.

**Files:**
- Create: `src/ui/page_jog.cpp`, `page_home.cpp`, `page_probe.cpp`, `page_offsets.cpp`,
  `page_run.cpp`, `page_macro.cpp`, `page_diag.cpp`, `page_setup.cpp`

- [ ] **Step 1: Write a stub for each of the eight pages**

Pattern — `src/ui/page_macro.cpp` shown in full; repeat with the matching names for
`page_home`, `page_setup`:

```cpp
#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"

void page_macro_build(lv_obj_t* p) {
    lv_obj_t* l = w_label(p, "Macros", &lv_font_montserrat_20, C_DIM);
    lv_obj_center(l);
}
```

For the four pages that declare a tick, add the empty tick too — for example
`src/ui/page_diag.cpp`:

```cpp
#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"

void page_diag_build(lv_obj_t* p) {
    lv_obj_t* l = w_label(p, "Diagnostics", &lv_font_montserrat_20, C_DIM);
    lv_obj_center(l);
}

void page_diag_tick() {}
```

`page_jog.cpp`, `page_offsets.cpp` and `page_run.cpp` follow the same shape with
`page_jog_tick()`, `page_offsets_tick()` and `page_run_tick()`.

- [ ] **Step 2: Verify it builds and links**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`, with a RAM/Flash summary printed.

- [ ] **Step 3: Commit**

```bash
git add src/ui/page_*.cpp
git commit -m "ui: page stubs so the shell links"
```

---

## Task 12: Wire up main.cpp

**Files:**
- Modify: `src/main.cpp` (replace the stub from Task 1 entirely)

- [ ] **Step 1: Write `src/main.cpp`**

```cpp
#include <Arduino.h>
#include "hw/display_hw.h"
#include "hw/touch.h"
#include "transport/uart.h"
#include "model/FluidNCModel.h"
#include "model/model_events.h"
#include "model/console_log.h"
#include "model/offsets.h"
#include "ui/shell.h"
#include "GrblParserC.h"

extern "C" bool fnc_rx_waiting();

static void ui_tick_cb(lv_timer_t*) { shell_tick(); }

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== CNC Console ===");

    display_init();
    lvgl_init();
    set_backlight(200);

    clog_reset();
    offsets_reset();

    init_fnc_uart();

    shell_build();
    lv_timer_create(ui_tick_cb, 50, nullptr);   // 20 Hz

    Serial.printf("PSRAM free: %u\n", (unsigned)ESP.getFreePsram());
    Serial.printf("Heap free:  %u\n", (unsigned)ESP.getFreeHeap());

    fnc_putchar(0x11);                          // XON, clear any stale XOFF
    fnc_realtime(StatusReport);
    send_line("$#");                            // prime the work offsets
}

void loop() {
    // fnc_poll() drains one byte. The link can deliver far more per tick than
    // that, so drain a chunk and stop as soon as RX is empty — draining blindly
    // adds idle-loop jitter that makes small jog moves choppy.
    for (int i = 0; i < 64; i++) {
        fnc_poll();
        if (!fnc_rx_waiting()) break;
    }
    lv_timer_handler();
}
```

- [ ] **Step 2: Add the `handle_other` hook so unmatched lines reach the console and offsets**

Append to `src/model/model_events.cpp`:

```cpp
#include "model/offsets.h"

// GrblParserC calls this for any line it does not recognise. Two consumers:
// the work-offset table ($# replies) and the console.
extern "C" void handle_other_line(char* line) {
    if (!line) return;
    offsets_feed_line(line);
    clog_add(line);
}
```

Then in `src/model/FluidNCModel.cpp`, find `extern "C" void handle_other(char* line)` and add
`handle_other_line(line);` as its first statement, keeping the existing body.

- [ ] **Step 3: Verify it builds**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp src/model/model_events.cpp src/model/FluidNCModel.cpp
git commit -m "app: wire panel, UART, model and shell together"
```

- [ ] **Step 5: [HW] Flash and verify Stage 1**

```bash
pio run -e crowpanel -t upload
pio device monitor
```

Expected on the panel: nav rail on the left with JOG lit, DRO reading `0.000` on all three
axes, `NO LINK` state chip until the UART is connected, HOLD/RESUME/RESET across the bottom.

Expected once wired to FluidNC: the state chip changes to `IDLE`, and jogging the machine from
another sender makes the DRO numbers track.

If the panel stays black, check GPIO38 is being driven low and the backlight is on GPIO2. If
touch does nothing, check the PCA9557 reset ran — the boot log prints `PCA9557 touch reset done`.

---

## Task 13: Jog page

**Files:**
- Modify: `src/ui/page_jog.cpp` (replace the stub)

- [ ] **Step 1: Write `src/ui/page_jog.cpp`**

```cpp
#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <stdio.h>

// 240 pad | 88 Z column | remainder
static const lv_coord_t PAD    = U * 30;   // 240
static const lv_coord_t ZCOL_W = U * 11;   // 88
static const lv_coord_t KEY    = U * 10;   // 80

static const float STEPS[4] = { 0.01f, 0.1f, 1.0f, 10.0f };
static int   s_step = 2;
static lv_obj_t* s_step_btn[4];
static lv_obj_t* s_mid_lbl;

// dx, dy, dz per pad slot; {0,0,0} is the centre, which is not a button.
struct dir_t { int dx, dy, dz; };
static const dir_t PADDIR[9] = {
    { -1,  1, 0 }, { 0,  1, 0 }, { 1,  1, 0 },
    { -1,  0, 0 }, { 0,  0, 0 }, { 1,  0, 0 },
    { -1, -1, 0 }, { 0, -1, 0 }, { 1, -1, 0 },
};

static void jog_start(lv_event_t* e) {
    const dir_t* d = (const dir_t*)lv_event_get_user_data(e);
    if (!d || (!d->dx && !d->dy && !d->dz)) return;
    char buf[64];
    float s = STEPS[s_step];
    int n = snprintf(buf, sizeof(buf), "$J=G91F2000");
    if (d->dx) n += snprintf(buf + n, sizeof(buf) - n, "X%.3f", d->dx * s);
    if (d->dy) n += snprintf(buf + n, sizeof(buf) - n, "Y%.3f", d->dy * s);
    if (d->dz) n += snprintf(buf + n, sizeof(buf) - n, "Z%.3f", d->dz * s);
    send_jog_line(buf);
}

// Cancel on release AND on press-lost. Press-lost is the finger sliding off the
// key; without it the axis keeps moving after the thumb reaches the bezel.
static void jog_stop(lv_event_t*) { send_jog_cancel(); }

static void attach_jog(lv_obj_t* btn, const dir_t* d) {
    lv_obj_add_event_cb(btn, jog_start, LV_EVENT_PRESSED,    (void*)d);
    lv_obj_add_event_cb(btn, jog_stop,  LV_EVENT_RELEASED,   (void*)d);
    lv_obj_add_event_cb(btn, jog_stop,  LV_EVENT_PRESS_LOST, (void*)d);
}

static void step_cb(lv_event_t* e) {
    s_step = (int)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < 4; i++) {
        bool on = (i == s_step);
        lv_obj_set_style_bg_color(s_step_btn[i], on ? C_ACCENT : C_RAISE, 0);
    }
    char b[8];
    snprintf(b, sizeof(b), "%g", STEPS[s_step]);
    lv_label_set_text(s_mid_lbl, b);
}

static void zero_cb(lv_event_t* e) {
    int axis = (int)(intptr_t)lv_event_get_user_data(e);
    // G10 L20 P0 sets the *active* work system, which is why the DRO shows which
    // one is live — zeroing the wrong system is otherwise silent.
    if (axis < 0) send_line("G10 L20 P0 X0 Y0 Z0");
    else          send_linef("G10 L20 P0 %c0", axisNumToChar(axis));
}

static void motors_off_cb(lv_event_t*) { send_line("$MD"); }

void page_jog_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(p, U, 0);

    // --- XY pad ---
    lv_obj_t* pad = lv_obj_create(p);
    w_flatten(pad);
    lv_obj_set_size(pad, PAD, PAD);
    lv_obj_set_layout(pad, LV_LAYOUT_GRID);
    static lv_coord_t pc[] = { KEY, KEY, KEY, LV_GRID_TEMPLATE_LAST };
    static lv_coord_t pr[] = { KEY, KEY, KEY, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(pad, pc, pr);

    static const char* GLYPH[9] = {
        LV_SYMBOL_UP, LV_SYMBOL_UP, LV_SYMBOL_UP,
        LV_SYMBOL_LEFT, "", LV_SYMBOL_RIGHT,
        LV_SYMBOL_DOWN, LV_SYMBOL_DOWN, LV_SYMBOL_DOWN
    };
    for (int i = 0; i < 9; i++) {
        if (i == 4) {
            lv_obj_t* mid = lv_obj_create(pad);
            w_flatten(mid);
            lv_obj_set_grid_cell(mid, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
            lv_obj_t* cap = w_label(mid, "STEP", &lv_font_montserrat_10, C_DIM);
            lv_obj_align(cap, LV_ALIGN_CENTER, 0, -10);
            s_mid_lbl = w_label(mid, "1", &lv_font_montserrat_20, C_ACCENT);
            lv_obj_align(s_mid_lbl, LV_ALIGN_CENTER, 0, 8);
            continue;
        }
        lv_obj_t* b = w_button(pad, GLYPH[i], nullptr, C_RAISE, C_TEXT, nullptr, nullptr);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
        attach_jog(b, &PADDIR[i]);
    }

    // --- Z column ---
    lv_obj_t* zc = lv_obj_create(p);
    w_flatten(zc);
    lv_obj_set_size(zc, ZCOL_W, PAD);
    lv_obj_set_flex_flow(zc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(zc, 2, 0);
    static const dir_t ZUP   = { 0, 0,  1 };
    static const dir_t ZDOWN = { 0, 0, -1 };
    lv_obj_t* zu = w_button(zc, "Z+", nullptr, C_RAISE, C_TEXT, nullptr, nullptr);
    lv_obj_set_size(zu, LV_PCT(100), (PAD - 2) / 2);
    attach_jog(zu, &ZUP);
    lv_obj_t* zd = w_button(zc, "Z-", nullptr, C_RAISE, C_TEXT, nullptr, nullptr);
    lv_obj_set_size(zd, LV_PCT(100), (PAD - 2) / 2);
    attach_jog(zd, &ZDOWN);

    // --- right column: steps over zero buttons ---
    lv_obj_t* rc = lv_obj_create(p);
    w_flatten(rc);
    lv_obj_set_size(rc, LV_PCT(100), PAD);
    lv_obj_set_flex_flow(rc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(rc, U, 0);
    lv_obj_set_flex_grow(rc, 1);

    lv_obj_t* steps = lv_obj_create(rc);
    w_flatten(steps);
    lv_obj_set_size(steps, LV_PCT(100), U * 8);
    lv_obj_set_flex_flow(steps, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(steps, U, 0);
    static const char* SLBL[4] = { "0.01", "0.1", "1.0", "10" };
    for (int i = 0; i < 4; i++) {
        s_step_btn[i] = w_button(steps, SLBL[i], "mm",
                                 i == s_step ? C_ACCENT : C_RAISE, C_TEXT,
                                 step_cb, (void*)(intptr_t)i);
        lv_obj_set_flex_grow(s_step_btn[i], 1);
        lv_obj_set_height(s_step_btn[i], LV_PCT(100));
    }

    lv_obj_t* zeros = lv_obj_create(rc);
    w_flatten(zeros);
    lv_obj_set_size(zeros, LV_PCT(100), U * 21);
    lv_obj_set_layout(zeros, LV_LAYOUT_GRID);
    static lv_coord_t zcs[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t zrs[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(zeros, zcs, zrs);
    lv_obj_set_style_pad_gap(zeros, U, 0);

    struct { const char* t; const char* s; int axis; lv_color_t bg; } Z[4] = {
        { "X ZERO", "G10 L20 P0", 0,  C_SURFACE },
        { "Y ZERO", "G10 L20 P0", 1,  C_SURFACE },
        { "Z ZERO", "G10 L20 P0", 2,  C_SURFACE },
        { "STEPPERS OFF", "$MD",  -2, C_RAISE   },
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* b;
        if (Z[i].axis == -2) {
            b = w_button(zeros, Z[i].t, Z[i].s, Z[i].bg, C_WARN, motors_off_cb, nullptr);
        } else {
            b = w_button(zeros, Z[i].t, Z[i].s, Z[i].bg, C_TEXT, zero_cb,
                         (void*)(intptr_t)Z[i].axis);
        }
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_STRETCH, i / 2, 1);
    }
}

void page_jog_tick() {}
```

- [ ] **Step 2: Verify it builds**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`

- [ ] **Step 3: Commit**

```bash
git add src/ui/page_jog.cpp
git commit -m "ui: jog page with press-lost cancel and steppers-off"
```

- [ ] **Step 4: [HW] Verify jogging**

Flash, then on the panel: select a step size, tap a pad key, confirm the axis moves that
distance. **Then press a key and slide your finger off it without lifting** — the axis must
stop. That is the `LV_EVENT_PRESS_LOST` path and it is the single most important behaviour on
this page.

---

## Task 14: Home page

**Files:**
- Modify: `src/ui/page_home.cpp`

- [ ] **Step 1: Write `src/ui/page_home.cpp`**

```cpp
#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"

static void home_all_cb(lv_event_t*) { send_line("$H"); }

static void home_axis_cb(lv_event_t* e) {
    int axis = (int)(intptr_t)lv_event_get_user_data(e);
    send_linef("$H%c", axisNumToChar(axis));
}

static void unlock_cb(lv_event_t*) { send_line("$X"); }

void page_home_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(p, U, 0);

    lv_obj_t* left = lv_obj_create(p);
    w_flatten(left);
    lv_obj_set_size(left, U * 44, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(left, U, 0);

    lv_obj_t* all = w_button(left, "HOME ALL", "$H", C_RUN, C_BG, home_all_cb, nullptr);
    lv_obj_set_size(all, LV_PCT(100), U * 14);

    lv_obj_t* row = lv_obj_create(left);
    w_flatten(row);
    lv_obj_set_size(row, LV_PCT(100), U * 14);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(row, U, 0);
    static const char* AXL[3] = { "HOME X", "HOME Y", "HOME Z" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* b = w_button(row, AXL[i], nullptr, C_SURFACE, C_TEXT,
                               home_axis_cb, (void*)(intptr_t)i);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_height(b, LV_PCT(100));
    }

    lv_obj_t* right = lv_obj_create(p);
    w_flatten(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(right, U, 0);

    lv_obj_t* unlock = w_button(right, "UNLOCK", "$X", C_WARN, C_BG, unlock_cb, nullptr);
    lv_obj_set_size(unlock, LV_PCT(100), U * 14);

    lv_obj_t* note = w_panel(right, C_SURFACE);
    lv_obj_set_size(note, LV_PCT(100), U * 14);
    lv_obj_t* nl = w_label(note, "Homing moves at full rate.\nClear the table first.",
                           &lv_font_montserrat_12, C_DIM);
    lv_obj_center(nl);
    lv_obj_set_style_text_align(nl, LV_TEXT_ALIGN_CENTER, 0);
}
```

- [ ] **Step 2: Verify it builds**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`

- [ ] **Step 3: Commit**

```bash
git add src/ui/page_home.cpp
git commit -m "ui: home page with all-axis and per-axis homing"
```

---

## Task 15: Probe page

**Files:**
- Modify: `src/ui/page_probe.cpp`

- [ ] **Step 1: Write `src/ui/page_probe.cpp`**

The G-code is `FluidDial-main/src/ProbingScene.cpp:29` — do not re-derive it.

```cpp
#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <stdio.h>

struct param_t {
    const char* name;
    float       value;
    float       step;
    const char* unit;
    lv_obj_t*   lbl;
};

static param_t s_params[4] = {
    { "Tool offset", 0.0f,  0.1f, "mm",     nullptr },
    { "Max travel",  25.0f, 1.0f, "mm",     nullptr },
    { "Feed rate",   100.0f, 10.0f, "mm/m", nullptr },
    { "Retract",     2.0f,  0.5f, "mm",     nullptr },
};

static lv_obj_t* s_probe_state;
static lv_obj_t* s_gcode_lbl;

static void refresh_param(int i) {
    char b[24];
    snprintf(b, sizeof(b), "%.2f", s_params[i].value);
    lv_label_set_text(s_params[i].lbl, b);
}

static void refresh_gcode() {
    char b[64];
    snprintf(b, sizeof(b), "G38.2 G91 F%d Z-%d",
             (int)s_params[2].value, (int)s_params[1].value);
    lv_label_set_text(s_gcode_lbl, b);
}

static void bump_cb(lv_event_t* e) {
    int packed = (int)(intptr_t)lv_event_get_user_data(e);
    int idx = packed >> 1;
    int dir = (packed & 1) ? 1 : -1;
    s_params[idx].value += dir * s_params[idx].step;
    if (s_params[idx].value < 0) s_params[idx].value = 0;
    refresh_param(idx);
    refresh_gcode();
}

static void probe_cb(lv_event_t*) {
    // Probe down on Z, then retract. Matches ProbingScene.cpp:29 and :51.
    send_linef("G38.2G91F%dZ-%dP%.2f",
               (int)s_params[2].value, (int)s_params[1].value, s_params[0].value);
    send_linef("$J=G91F1000Z%.2f", s_params[3].value);
}

void page_probe_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(p, U, 0);

    lv_obj_t* left = lv_obj_create(p);
    w_flatten(left);
    lv_obj_set_size(left, U * 55, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(left, U, 0);

    w_label(left, "PROBE PARAMETERS - G38.2", &lv_font_montserrat_10, C_DIM);

    for (int i = 0; i < 4; i++) {
        lv_obj_t* row = w_panel(left, C_SURFACE);
        lv_obj_set_size(row, LV_PCT(100), U * 6);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(row, U + 4, 0);

        lv_obj_t* nm = w_label(row, s_params[i].name, &lv_font_montserrat_14, C_TEXT);
        lv_obj_set_flex_grow(nm, 1);

        lv_obj_t* minus = w_button(row, "-", nullptr, C_RAISE, C_ACCENT,
                                   bump_cb, (void*)(intptr_t)((i << 1) | 0));
        lv_obj_set_size(minus, U * 6, LV_PCT(100));

        s_params[i].lbl = w_label(row, "0.00", &lv_font_montserrat_16, C_TEXT);
        lv_obj_set_width(s_params[i].lbl, U * 10);
        lv_obj_set_style_text_align(s_params[i].lbl, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t* plus = w_button(row, "+", nullptr, C_RAISE, C_ACCENT,
                                  bump_cb, (void*)(intptr_t)((i << 1) | 1));
        lv_obj_set_size(plus, U * 6, LV_PCT(100));

        refresh_param(i);
    }

    lv_obj_t* right = lv_obj_create(p);
    w_flatten(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(right, U, 0);

    lv_obj_t* st = w_panel(right, C_SURFACE);
    lv_obj_set_size(st, LV_PCT(100), U * 14);
    lv_obj_t* stk = w_label(st, "PROBE INPUT", &lv_font_montserrat_10, C_DIM);
    lv_obj_align(stk, LV_ALIGN_CENTER, 0, -12);
    s_probe_state = w_label(st, "OPEN", &lv_font_montserrat_20, C_OK);
    lv_obj_align(s_probe_state, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t* go = lv_btn_create(right);
    lv_obj_set_size(go, LV_PCT(100), U * 14);
    lv_obj_set_style_bg_color(go, C_WARN, 0);
    lv_obj_set_style_radius(go, 3, 0);
    lv_obj_set_style_shadow_width(go, 0, 0);
    lv_obj_add_event_cb(go, probe_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* gl = w_label(go, "PROBE Z", &lv_font_montserrat_20, C_BG);
    lv_obj_align(gl, LV_ALIGN_CENTER, 0, -10);
    s_gcode_lbl = w_label(go, "", &lv_font_montserrat_12, C_BG);
    lv_obj_align(s_gcode_lbl, LV_ALIGN_CENTER, 0, 12);
    refresh_gcode();
}
```

- [ ] **Step 2: Verify it builds**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`

- [ ] **Step 3: Commit**

```bash
git add src/ui/page_probe.cpp
git commit -m "ui: probe page reusing FluidDial's G38.2 sequence"
```

---

## Task 16: Offsets page

**Files:**
- Modify: `src/ui/page_offsets.cpp`

- [ ] **Step 1: Write `src/ui/page_offsets.cpp`**

```cpp
#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/offsets.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <string.h>

#define VISIBLE_ROWS 6

static lv_obj_t* s_name[VISIBLE_ROWS];
static lv_obj_t* s_cell[VISIBLE_ROWS][3];
static lv_obj_t* s_rowobj[VISIBLE_ROWS];
static int       s_sel = 0;

static void row_pick_cb(lv_event_t* e) {
    s_sel = (int)(intptr_t)lv_event_get_user_data(e);
    page_offsets_tick();
}

// Every action here writes persistent machine state, so every button shows the
// G-code it sends. If it does something unintended you can see what it did.
static void set_here_cb(lv_event_t*) {
    if (s_sel > 5) return;                    // only G54..G59 are settable this way
    send_linef("G10 L20 P%d X0 Y0 Z0", s_sel + 1);
    send_line("$#");
}

static void goto_origin_cb(lv_event_t*) {
    if (s_sel > 5) return;
    send_linef("G%d G0 X0 Y0", 54 + s_sel);
}

static void clear_g92_cb(lv_event_t*) {
    send_line("G92.1");
    send_line("$#");
}

static void activate_cb(lv_event_t*) {
    if (s_sel > 5) return;
    send_linef("G%d", 54 + s_sel);
    send_line("$#");
}

void page_offsets_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(p, U, 0);

    lv_obj_t* tab = lv_obj_create(p);
    w_flatten(tab);
    lv_obj_set_size(tab, U * 60, LV_PCT(100));
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(tab, 2, 0);

    lv_obj_t* head = lv_obj_create(tab);
    w_flatten(head);
    lv_obj_set_size(head, LV_PCT(100), U * 3);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_t* h0 = w_label(head, "SYSTEM", &lv_font_montserrat_10, C_DIM);
    lv_obj_set_width(h0, U * 8);
    static const char* HX[3] = { "X", "Y", "Z" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* h = w_label(head, HX[i], &lv_font_montserrat_10, C_DIM);
        lv_obj_set_flex_grow(h, 1);
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_RIGHT, 0);
    }

    for (int r = 0; r < VISIBLE_ROWS; r++) {
        lv_obj_t* row = lv_btn_create(tab);
        lv_obj_set_size(row, LV_PCT(100), U * 4);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, C_SURFACE, 0);
        lv_obj_set_style_border_color(row, C_LINE, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_pad_left(row, U + 2, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, row_pick_cb, LV_EVENT_CLICKED, (void*)(intptr_t)r);
        s_rowobj[r] = row;

        s_name[r] = w_label(row, "---", &lv_font_montserrat_14, C_DIM);
        lv_obj_set_width(s_name[r], U * 8);

        for (int c = 0; c < 3; c++) {
            s_cell[r][c] = w_label(row, "0.000", &lv_font_montserrat_14, C_TEXT);
            lv_obj_set_flex_grow(s_cell[r][c], 1);
            lv_obj_set_style_text_align(s_cell[r][c], LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_pad_right(s_cell[r][c], U + 2, 0);
        }
    }

    lv_obj_t* acts = lv_obj_create(p);
    w_flatten(acts);
    lv_obj_set_flex_grow(acts, 1);
    lv_obj_set_height(acts, LV_PCT(100));
    lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(acts, U, 0);

    struct { const char* t; const char* s; lv_event_cb_t cb; lv_color_t bg; lv_color_t fg; } A[4] = {
        { "SET FROM HERE", "G10 L20 Pn",   set_here_cb,     C_ACCENT,  C_BG   },
        { "GO TO ORIGIN",  "Gnn G0 X0 Y0", goto_origin_cb,  C_RAISE,   C_TEXT },
        { "CLEAR G92",     "G92.1",        clear_g92_cb,    C_RAISE,   C_TEXT },
        { "ACTIVATE",      "Gnn",          activate_cb,     C_RAISE,   C_TEXT },
    };
    for (auto& a : A) {
        lv_obj_t* b = w_button(acts, a.t, a.s, a.bg, a.fg, a.cb, nullptr);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_flex_grow(b, 1);
    }
}

void page_offsets_tick() {
    for (int r = 0; r < VISIBLE_ROWS; r++) {
        const wcs_t* w = offsets_row(r);
        if (!w) continue;
        lv_label_set_text(s_name[r], w->name);
        for (int c = 0; c < 3; c++) {
            char b[16];
            if (w->valid) snprintf(b, sizeof(b), "%.3f", w->axis[c]);
            else          snprintf(b, sizeof(b), "---");
            lv_label_set_text(s_cell[r][c], b);
        }
        bool sel = (r == s_sel);
        lv_obj_set_style_bg_color(s_rowobj[r], sel ? C_SEL_BG : C_SURFACE, 0);
        lv_obj_set_style_border_color(s_rowobj[r], sel ? C_ACCENT : C_LINE, 0);
        lv_obj_set_style_text_color(s_name[r], sel ? C_ACCENT : C_DIM, 0);
    }
}
```

- [ ] **Step 2: Verify it builds**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`

- [ ] **Step 3: Commit**

```bash
git add src/ui/page_offsets.cpp
git commit -m "ui: work offsets page backed by \$#"
```

---

## Task 17: Run page and file listing

**Files:**
- Modify: `src/ui/page_run.cpp`
- Create: `src/model/filelist.h`, `src/model/filelist.cpp`

- [ ] **Step 1: Write `src/model/filelist.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

#define FL_MAX_ENTRIES 64
#define FL_NAME_MAX    64

struct fl_entry_t {
    char     name[FL_NAME_MAX];
    uint32_t size;
    bool     is_dir;
};

void  filelist_reset();
void  filelist_request();                    // sends $Files/ListGCode
void  filelist_feed_line(const char* line);  // consumes the JSON reply
size_t filelist_count();
const fl_entry_t* filelist_at(size_t i);
const char* filelist_path();
void  filelist_enter(const char* name);      // descend, or ".." to go up
bool  filelist_dirty_take();
```

- [ ] **Step 2: Write `src/model/filelist.cpp`**

FluidNC answers `$Files/ListGCode` with an encapsulated JSON document. Rather than pull in the
streaming parser for one screen, scan for the `{"name":"x","size":"n"}` shape directly — the
reply format is fixed and this is 40 lines instead of a listener class.

```cpp
#include "model/filelist.h"
#include "model/FluidNCModel.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static fl_entry_t s_entries[FL_MAX_ENTRIES];
static size_t     s_count = 0;
static char       s_path[128] = "/sd";
static bool       s_dirty = false;

void filelist_reset() {
    s_count = 0;
    s_dirty = true;
}

const char* filelist_path() { return s_path; }
size_t filelist_count()     { return s_count; }

const fl_entry_t* filelist_at(size_t i) {
    return (i < s_count) ? &s_entries[i] : nullptr;
}

bool filelist_dirty_take() { bool d = s_dirty; s_dirty = false; return d; }

void filelist_request() {
    filelist_reset();
    send_linef("$Files/ListGCode=%s", s_path);
}

// Pull the value of "key":"value" or "key":value starting at p. Returns nullptr if absent.
static const char* field(const char* p, const char* key, char* out, size_t outn) {
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* k = strstr(p, pat);
    if (!k) return nullptr;
    const char* c = strchr(k, ':');
    if (!c) return nullptr;
    c++;
    while (*c == ' ') c++;
    bool quoted = (*c == '"');
    if (quoted) c++;
    size_t n = 0;
    while (*c && n < outn - 1) {
        if (quoted ? (*c == '"') : (*c == ',' || *c == '}')) break;
        out[n++] = *c++;
    }
    out[n] = 0;
    return c;
}

void filelist_feed_line(const char* line) {
    if (!line) return;
    const char* p = line;
    while ((p = strchr(p, '{')) != nullptr) {
        if (s_count >= FL_MAX_ENTRIES) break;
        char name[FL_NAME_MAX] = { 0 };
        char size[24] = { 0 };
        const char* after = field(p, "name", name, sizeof(name));
        if (!after || !name[0]) { p++; continue; }
        field(p, "size", size, sizeof(size));

        fl_entry_t& e = s_entries[s_count];
        strncpy(e.name, name, FL_NAME_MAX - 1);
        e.name[FL_NAME_MAX - 1] = 0;
        // FluidNC reports directories with size -1.
        e.is_dir = (size[0] == '-') || (size[0] == 0);
        e.size   = e.is_dir ? 0 : (uint32_t)strtoul(size, nullptr, 10);
        s_count++;
        s_dirty = true;
        p++;
    }
}

void filelist_enter(const char* name) {
    if (!name) return;
    if (strcmp(name, "..") == 0) {
        char* slash = strrchr(s_path, '/');
        if (slash && slash != s_path) *slash = 0;
    } else {
        size_t l = strlen(s_path);
        snprintf(s_path + l, sizeof(s_path) - l, "/%s", name);
    }
    filelist_request();
}
```

- [ ] **Step 3: Route unmatched lines into the file list**

In `src/model/model_events.cpp`, extend `handle_other_line`:

```cpp
#include "model/filelist.h"

extern "C" void handle_other_line(char* line) {
    if (!line) return;
    offsets_feed_line(line);
    filelist_feed_line(line);
    clog_add(line);
}
```

- [ ] **Step 4: Write `src/ui/page_run.cpp`**

```cpp
#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/view_expanded.h"
#include "model/filelist.h"
#include "model/FluidNCModel.h"
#include "model/gcode_path.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <string.h>

#define ROWS 3

static lv_obj_t* s_crumb;
static lv_obj_t* s_row[ROWS];
static lv_obj_t* s_row_name[ROWS];
static lv_obj_t* s_row_size[ROWS];
static lv_obj_t* s_preview;
static int  s_sel = 0;
static int  s_scroll = 0;
static char s_selected[FL_NAME_MAX] = { 0 };

static void row_cb(lv_event_t* e) {
    int r = (int)(intptr_t)lv_event_get_user_data(e);
    const fl_entry_t* en = filelist_at(s_scroll + r);
    if (!en) return;
    if (en->is_dir) { filelist_enter(en->name); s_scroll = 0; s_sel = 0; return; }
    s_sel = s_scroll + r;
    strncpy(s_selected, en->name, sizeof(s_selected) - 1);
    page_run_tick();
}

static void run_cb(lv_event_t*) {
    if (!s_selected[0]) return;
    send_linef("$SD/Run=%s/%s", filelist_path(), s_selected);
}

// Check mode runs the file through the parser with no motion. Syntax errors and
// out-of-travel moves surface before the cutter is near the work.
static void dry_cb(lv_event_t*) { send_line("$C"); }

static void expand_cb(lv_event_t*) { expanded_open(EXPANDED_TOOLPATH); }

void page_run_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(p, U, 0);

    lv_obj_t* list = lv_obj_create(p);
    w_flatten(list);
    lv_obj_set_size(list, U * 37, LV_PCT(100));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(list, 2, 0);

    lv_obj_t* cb = w_panel(list, C_SURFACE);
    lv_obj_set_size(cb, LV_PCT(100), U * 5);
    s_crumb = w_label(cb, "/sd", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(s_crumb, LV_ALIGN_LEFT_MID, U + 2, 0);

    for (int r = 0; r < ROWS; r++) {
        lv_obj_t* row = lv_btn_create(list);
        lv_obj_set_size(row, LV_PCT(100), U * 8);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, C_RAISE, 0);
        lv_obj_set_style_border_color(row, C_LINE, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_pad_left(row, U + 2, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void*)(intptr_t)r);
        s_row[r] = row;
        s_row_name[r] = w_label(row, "", &lv_font_montserrat_14, C_TEXT);
        lv_obj_set_flex_grow(s_row_name[r], 1);
        s_row_size[r] = w_label(row, "", &lv_font_montserrat_10, C_DIM);
        lv_obj_set_style_pad_right(s_row_size[r], U, 0);
    }

    s_preview = lv_btn_create(p);
    lv_obj_set_size(s_preview, U * 37, LV_PCT(100));
    lv_obj_set_style_bg_color(s_preview, C_SURFACE, 0);
    lv_obj_set_style_border_color(s_preview, C_LINE, 0);
    lv_obj_set_style_border_width(s_preview, 1, 0);
    lv_obj_set_style_radius(s_preview, 3, 0);
    lv_obj_set_style_shadow_width(s_preview, 0, 0);
    lv_obj_add_event_cb(s_preview, expand_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* pv = w_label(s_preview, "TOOLPATH\ntap to expand", &lv_font_montserrat_12, C_DIM);
    lv_obj_center(pv);
    lv_obj_set_style_text_align(pv, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* acts = lv_obj_create(p);
    w_flatten(acts);
    lv_obj_set_flex_grow(acts, 1);
    lv_obj_set_height(acts, LV_PCT(100));
    lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(acts, U, 0);

    lv_obj_t* run = w_button(acts, "RUN", nullptr, C_OK, C_BG, run_cb, nullptr);
    lv_obj_set_size(run, LV_PCT(100), U * 13);
    lv_obj_t* dry = w_button(acts, "DRY", "$C", C_RAISE, C_TEXT, dry_cb, nullptr);
    lv_obj_set_size(dry, LV_PCT(100), U * 7);
    lv_obj_t* rel = w_button(acts, "RELOAD", nullptr, C_RAISE, C_TEXT,
                             [](lv_event_t*) { filelist_request(); }, nullptr);
    lv_obj_set_size(rel, LV_PCT(100), U * 7);
}

void page_run_tick() {
    if (!filelist_dirty_take()) return;
    lv_label_set_text(s_crumb, filelist_path());
    for (int r = 0; r < ROWS; r++) {
        const fl_entry_t* e = filelist_at(s_scroll + r);
        if (!e) {
            lv_label_set_text(s_row_name[r], "");
            lv_label_set_text(s_row_size[r], "");
            lv_obj_set_style_bg_color(s_row[r], C_SURFACE, 0);
            continue;
        }
        lv_label_set_text(s_row_name[r], e->name);
        char b[16];
        if (e->is_dir) snprintf(b, sizeof(b), "DIR");
        else if (e->size >= 1024 * 1024) snprintf(b, sizeof(b), "%.1f MB", e->size / 1048576.0);
        else snprintf(b, sizeof(b), "%u KB", (unsigned)(e->size / 1024));
        lv_label_set_text(s_row_size[r], b);

        bool sel = ((int)(s_scroll + r) == s_sel) && !e->is_dir;
        lv_obj_set_style_bg_color(s_row[r], sel ? C_SEL_BG : C_RAISE, 0);
        lv_obj_set_style_border_color(s_row[r], sel ? C_ACCENT : C_LINE, 0);
    }
}
```

- [ ] **Step 5: Verify it builds**

Run: `pio run -e crowpanel`
Expected: link error for `expanded_open` — Task 18 supplies it.

- [ ] **Step 6: Commit**

```bash
git add src/model/filelist.h src/model/filelist.cpp src/ui/page_run.cpp src/model/model_events.cpp
git commit -m "ui: run page with G-code file listing"
```

---

## Task 18: Expanded view — toolpath and console

**Files:**
- Create: `src/ui/view_expanded.h`, `src/ui/view_expanded.cpp`

- [ ] **Step 1: Write `src/ui/view_expanded.h`**

```cpp
#pragma once
#include <lvgl.h>

enum expanded_face_t { EXPANDED_TOOLPATH, EXPANDED_CONSOLE };

void expanded_build();
void expanded_open(expanded_face_t face);
void expanded_close();
void expanded_flip();
bool expanded_is_open();
void expanded_tick();

// Move the live position marker. Called from the DRO tick.
void toolpath_set_marker(float x, float y);

// Redraw the whole path after a load.
void toolpath_redraw();
```

- [ ] **Step 2: Write `src/ui/view_expanded.cpp`**

The canvas is 800×400 — full width, everything above the safety bar. 90% of the panel height
would be 432 px, which would leave 48 px for hold/resume/reset; those keep their full 80 px.

```cpp
#include "ui/view_expanded.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/shell.h"
#include "model/gcode_path.h"
#include "model/console_log.h"
#include "model/FluidNCModel.h"
#include <stdio.h>
#include <string.h>

#define CANVAS_W 800
#define CANVAS_H 400
#define LOG_LINES 13

static lv_obj_t* s_root;
static lv_obj_t* s_tp_page;
static lv_obj_t* s_con_page;
static lv_obj_t* s_canvas;
static lv_obj_t* s_marker;
static lv_obj_t* s_tp_info;
static lv_obj_t* s_tp_dro;
static lv_obj_t* s_log_lbl[LOG_LINES];
static lv_obj_t* s_filter_lbl;
static bool s_open = false;
static expanded_face_t s_face = EXPANDED_TOOLPATH;

// Canvas buffer lives in PSRAM: 800*400*2 bytes is 640 KB, far past internal RAM.
static lv_color_t* s_cbuf = nullptr;

static float s_sx = 1.0f, s_sy = 1.0f, s_ox = 0.0f, s_oy = 0.0f;

bool expanded_is_open() { return s_open; }

static void fit_transform() {
    gextents_t e = gpath_extents();
    float w = e.max_x - e.min_x, h = e.max_y - e.min_y;
    if (w <= 0.0f) w = 1.0f;
    if (h <= 0.0f) h = 1.0f;
    const float M = 24.0f;                      // margin px
    float sx = (CANVAS_W - 2 * M) / w;
    float sy = (CANVAS_H - 2 * M) / h;
    float s  = sx < sy ? sx : sy;
    s_sx = s; s_sy = s;
    s_ox = M + ((CANVAS_W - 2 * M) - w * s) * 0.5f - e.min_x * s;
    s_oy = M + ((CANVAS_H - 2 * M) - h * s) * 0.5f + e.max_y * s;
}

static inline lv_coord_t px(float x) { return (lv_coord_t)(s_ox + x * s_sx); }
static inline lv_coord_t py(float y) { return (lv_coord_t)(s_oy - y * s_sy); }

void toolpath_redraw() {
    if (!s_canvas) return;
    lv_canvas_fill_bg(s_canvas, C_BG, LV_OPA_COVER);
    size_t n = gpath_count();
    if (n < 2) return;
    fit_transform();

    lv_draw_line_dsc_t feed, rapid;
    lv_draw_line_dsc_init(&feed);
    feed.color = C_ACCENT; feed.width = 2; feed.opa = LV_OPA_COVER;
    lv_draw_line_dsc_init(&rapid);
    rapid.color = C_LINE;  rapid.width = 1; rapid.opa = LV_OPA_COVER;

    lv_point_t seg[2];
    for (size_t i = 1; i < n; i++) {
        gpoint_t a = gpath_point(i - 1), b = gpath_point(i);
        seg[0].x = px(a.x); seg[0].y = py(a.y);
        seg[1].x = px(b.x); seg[1].y = py(b.y);
        lv_canvas_draw_line(s_canvas, seg, 2, b.rapid ? &rapid : &feed);
    }
}

void toolpath_set_marker(float x, float y) {
    if (!s_marker || !s_open || s_face != EXPANDED_TOOLPATH) return;
    lv_obj_set_pos(s_marker, px(x) - 8, py(y) - 8);
}

static void flip_cb(lv_event_t*)  { expanded_flip(); }
static void close_cb(lv_event_t*) { expanded_close(); }

static void filter_cb(lv_event_t*) {
    clog_set_show_status(!clog_show_status());
    lv_label_set_text(s_filter_lbl,
        clog_show_status() ? "STATUS FRAMES SHOWN" : "STATUS FRAMES HIDDEN");
}

static lv_color_t log_colour(clog_kind_t k) {
    switch (k) {
        case CLOG_SENT:  return C_TEXT;
        case CLOG_OK:    return lv_color_hex(0x48695A);
        case CLOG_ERROR: return C_CRIT;
        case CLOG_ALARM: return C_CRIT;
        case CLOG_MSG:   return C_WARN;
        default:         return C_DIM;
    }
}

void expanded_build() {
    s_root = lv_obj_create(lv_scr_act());
    w_flatten(s_root);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_size(s_root, CANVAS_W, CANVAS_H);
    lv_obj_set_style_bg_color(s_root, C_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    // --- toolpath face ---
    s_tp_page = lv_obj_create(s_root);
    w_flatten(s_tp_page);
    lv_obj_set_size(s_tp_page, CANVAS_W, CANVAS_H);

    s_cbuf = (lv_color_t*)ps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_W, CANVAS_H));
    s_canvas = lv_canvas_create(s_tp_page);
    if (s_cbuf) {
        lv_canvas_set_buffer(s_canvas, s_cbuf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(s_canvas, C_BG, LV_OPA_COVER);
    }
    lv_obj_set_pos(s_canvas, 0, 0);

    s_marker = lv_obj_create(s_tp_page);
    lv_obj_set_size(s_marker, 16, 16);
    lv_obj_set_style_radius(s_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_marker, C_WARN, 0);
    lv_obj_set_style_border_width(s_marker, 0, 0);
    lv_obj_clear_flag(s_marker, LV_OBJ_FLAG_SCROLLABLE);

    s_tp_info = w_label(s_tp_page, "no file loaded", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(s_tp_info, LV_ALIGN_TOP_LEFT, U + 2, U + 2);

    // The DRO does not vanish when the view expands, it moves. Losing position
    // readout on the screen you watch during a cut is a bad trade.
    s_tp_dro = w_label(s_tp_page, "X 0.000  Y 0.000  Z 0.000", &lv_font_montserrat_14, C_TEXT);
    lv_obj_align(s_tp_dro, LV_ALIGN_TOP_RIGHT, -(U + 2), U + 2);

    // --- console face ---
    s_con_page = lv_obj_create(s_root);
    w_flatten(s_con_page);
    lv_obj_set_size(s_con_page, CANVAS_W, CANVAS_H);
    lv_obj_add_flag(s_con_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_con_page, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* head = lv_obj_create(s_con_page);
    w_flatten(head);
    lv_obj_set_size(head, CANVAS_W, U * 4);
    lv_obj_set_style_bg_color(head, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_t* ht = w_label(head, "CONSOLE - UART 1 Mbaud", &lv_font_montserrat_10, C_DIM);
    lv_obj_align(ht, LV_ALIGN_LEFT_MID, U + 2, 0);
    lv_obj_t* fb = lv_btn_create(head);
    lv_obj_set_size(fb, U * 26, U * 3);
    lv_obj_align(fb, LV_ALIGN_RIGHT_MID, -(U), 0);
    lv_obj_set_style_bg_opa(fb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(fb, 0, 0);
    lv_obj_add_event_cb(fb, filter_cb, LV_EVENT_CLICKED, nullptr);
    s_filter_lbl = w_label(fb, "STATUS FRAMES HIDDEN", &lv_font_montserrat_10, C_ACCENT);
    lv_obj_center(s_filter_lbl);

    lv_obj_t* logbox = lv_obj_create(s_con_page);
    w_flatten(logbox);
    lv_obj_set_size(logbox, CANVAS_W, LOG_LINES * 24);
    lv_obj_set_style_pad_left(logbox, U + 4, 0);
    lv_obj_set_flex_flow(logbox, LV_FLEX_FLOW_COLUMN);
    for (int i = 0; i < LOG_LINES; i++) {
        s_log_lbl[i] = w_label(logbox, "", &lv_font_montserrat_12, C_DIM);
        lv_obj_set_height(s_log_lbl[i], 24);
    }
}

void expanded_open(expanded_face_t face) {
    s_open = true;
    s_face = face;
    shell_set_expanded(true);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);
    if (face == EXPANDED_TOOLPATH) {
        lv_obj_clear_flag(s_tp_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_con_page, LV_OBJ_FLAG_HIDDEN);
        toolpath_redraw();
    } else {
        lv_obj_add_flag(s_tp_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_con_page, LV_OBJ_FLAG_HIDDEN);
    }
}

void expanded_close() {
    s_open = false;
    shell_set_expanded(false);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

void expanded_flip() {
    expanded_open(s_face == EXPANDED_TOOLPATH ? EXPANDED_CONSOLE : EXPANDED_TOOLPATH);
}

void expanded_tick() {
    if (!s_open) return;
    if (s_face == EXPANDED_TOOLPATH) {
        char b[64];
        snprintf(b, sizeof(b), "X %.3f  Y %.3f  Z %.3f",
                 (double)myAxes[0] / 10000.0,
                 (double)myAxes[1] / 10000.0,
                 (double)myAxes[2] / 10000.0);
        lv_label_set_text(s_tp_dro, b);
    } else if (clog_dirty_take()) {
        size_t n = clog_count();
        size_t first = n > LOG_LINES ? n - LOG_LINES : 0;
        for (int i = 0; i < LOG_LINES; i++) {
            const clog_line_t* l = (first + i < n) ? clog_line(first + i) : nullptr;
            lv_label_set_text(s_log_lbl[i], l ? l->text : "");
            if (l) lv_obj_set_style_text_color(s_log_lbl[i], log_colour(l->kind), 0);
        }
    }
}
```

- [ ] **Step 3: Add FLIP and CLOSE to the safety bar**

In `src/ui/shell.cpp`, at the end of `build_safety()`, append:

```cpp
    lv_obj_t* flip = w_button(s_safety, "FLIP", "view", C_ACCENT, C_BG,
                              [](lv_event_t*) { expanded_flip(); }, nullptr);
    lv_obj_set_size(flip, U * 10, LV_PCT(100));
    lv_obj_add_flag(flip, LV_OBJ_FLAG_HIDDEN);
    s_flip_btn = flip;

    lv_obj_t* close = w_button(s_safety, "CLOSE", "back", C_RAISE, C_TEXT,
                               [](lv_event_t*) { expanded_close(); }, nullptr);
    lv_obj_set_size(close, U * 10, LV_PCT(100));
    lv_obj_add_flag(close, LV_OBJ_FLAG_HIDDEN);
    s_close_btn = close;
```

Declare `static lv_obj_t* s_flip_btn; static lv_obj_t* s_close_btn;` with the other statics, add
`#include "ui/view_expanded.h"`, and show or hide them in `shell_set_expanded()`:

```cpp
void shell_set_expanded(bool on) {
    s_expanded = on;
    if (on) {
        lv_obj_add_flag(s_rail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_dro,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_flip_btn,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_close_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_safety, 0, SCREEN_H - SAFETY_H);
        lv_obj_set_width(s_safety, SCREEN_W);
    } else {
        lv_obj_clear_flag(s_rail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_dro,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_flip_btn,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_close_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(s_safety, CONTENT_W);
    }
}
```

Add `expanded_tick();` to the end of `shell_tick()`.

- [ ] **Step 4: Call `expanded_build()` from `shell_build()`**

Add as the final line of `shell_build()`, after the page loop:

```cpp
    expanded_build();
```

- [ ] **Step 5: Verify it builds**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`

- [ ] **Step 6: Commit**

```bash
git add src/ui/view_expanded.h src/ui/view_expanded.cpp src/ui/shell.cpp
git commit -m "ui: expanded view flipping between toolpath and live console"
```

---

## Task 19: Load a toolpath over XModem

**Files:**
- Create: `src/model/path_loader.h`, `src/model/path_loader.cpp`
- Modify: `src/ui/page_run.cpp`

- [ ] **Step 1: Write `src/model/path_loader.h`**

```cpp
#pragma once
#include <stdbool.h>
#include <stddef.h>

// Pull <path> from the controller with $XS and parse it into the point buffer.
// Only legal when the machine is Idle or Alarm: FluidNC gates $XS on
// notIdleOrAlarm (FileCommands.cpp:676), so a file cannot be pulled mid-job.
bool  pathload_start(const char* path);
void  pathload_poll();
bool  pathload_busy();
bool  pathload_ok();
size_t pathload_bytes();
```

- [ ] **Step 2: Write `src/model/path_loader.cpp`**

```cpp
#include "model/path_loader.h"
#include "model/gcode_path.h"
#include "model/FluidNCModel.h"
#include "model/console_log.h"
#include "transport/xmodem_rx.h"
#include "transport/uart.h"
#include <string.h>
#include <stdio.h>

static bool s_busy = false;
static bool s_ok   = false;
static char s_line[256];
static size_t s_linelen = 0;

static void tx(uint8_t c) { fnc_putchar(c); }

// Split the incoming byte stream into lines and feed the parser. XModem pads the
// final block with CTRL-Z; those are discarded here rather than parsed.
static void sink(const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t c = d[i];
        if (c == 0x1A) continue;                 // CTRL-Z padding
        if (c == '\n' || c == '\r') {
            if (s_linelen) {
                s_line[s_linelen] = 0;
                gpath_feed_line(s_line);
                s_linelen = 0;
            }
            continue;
        }
        if (s_linelen < sizeof(s_line) - 1) s_line[s_linelen++] = (char)c;
    }
}

bool pathload_start(const char* path) {
    if (s_busy) return false;
    if (!(state == Idle || state == Alarm)) {
        clog_add("[MSG:Cannot load toolpath unless Idle or Alarm]");
        return false;
    }
    gpath_reset();
    s_linelen = 0;
    s_busy = true;
    s_ok   = false;
    xmodem_rx_begin(tx, sink);
    send_linef("$XS=%s", path);
    return true;
}

void pathload_poll() {
    if (!s_busy) return;
    if (xmodem_rx_done()) {
        s_busy = false;
        s_ok   = xmodem_rx_ok();
        if (s_linelen) { s_line[s_linelen] = 0; gpath_feed_line(s_line); s_linelen = 0; }
        char b[64];
        snprintf(b, sizeof(b), "[MSG:Toolpath %s, %u points]",
                 s_ok ? "loaded" : "failed", (unsigned)gpath_count());
        clog_add(b);
    }
}

bool   pathload_busy()  { return s_busy; }
bool   pathload_ok()    { return s_ok; }
size_t pathload_bytes() { return xmodem_rx_bytes(); }
```

- [ ] **Step 3: Route bytes to the loader while it is active**

In `src/main.cpp`, change the drain loop so XModem bytes bypass the G-code parser:

```cpp
void loop() {
    if (pathload_busy()) {
        // XModem owns the link during a transfer; the Grbl parser must not see
        // block headers or CRC bytes.
        for (int i = 0; i < 512; i++) {
            int c = fnc_getchar();
            if (c < 0) break;
            xmodem_rx_feed((uint8_t)c);
        }
        pathload_poll();
    } else {
        for (int i = 0; i < 64; i++) {
            fnc_poll();
            if (!fnc_rx_waiting()) break;
        }
    }
    lv_timer_handler();
}
```

Add `#include "model/path_loader.h"` and `#include "transport/xmodem_rx.h"` at the top.

- [ ] **Step 4: Trigger the load when a file is selected**

In `src/ui/page_run.cpp`, at the end of `row_cb()` after `strncpy(s_selected, ...)`, add:

```cpp
    char full[192];
    snprintf(full, sizeof(full), "%s/%s", filelist_path(), s_selected);
    pathload_start(full);
```

Add `#include "model/path_loader.h"`. In `page_run_tick()`, after the loop, add:

```cpp
    if (!pathload_busy() && gpath_count() > 0) toolpath_redraw();
```

- [ ] **Step 5: Verify it builds**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`

- [ ] **Step 6: Commit**

```bash
git add src/model/path_loader.h src/model/path_loader.cpp src/main.cpp src/ui/page_run.cpp
git commit -m "model: load toolpaths over XModem-1K"
```

- [ ] **Step 7: [HW] Measure the transfer**

Flash, select a real G-code file, and watch the serial monitor for
`[MSG:Toolpath loaded, N points]`. Time it. The spec estimates 10–15 s for 1 MB; if it is
dramatically slower, the fallback is `$File/ShowSome` line ranges — note the measured figure in
the spec's risk table either way.

---

## Task 20: Macro, Diag and Setup pages

**Files:**
- Modify: `src/ui/page_macro.cpp`, `src/ui/page_diag.cpp`, `src/ui/page_setup.cpp`

- [ ] **Step 1: Write `src/ui/page_diag.cpp`**

```cpp
#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/view_expanded.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"

struct diag_cmd_t { const char* label; const char* cmd; };

static const diag_cmd_t DIAG[6] = {
    { "SETTINGS",   "$S" },
    { "CHANGED",    "$SC" },
    { "LIMITS",     "$Limits/Show" },
    { "CHANNELS",   "$CI" },
    { "GPIO",       "$GD" },
    { "STATE",      "$T" },
};

static void diag_cb(lv_event_t* e) {
    const char* cmd = (const char*)lv_event_get_user_data(e);
    send_line(cmd);
    // Output arrives as ordinary lines, so show the console to read it.
    expanded_open(EXPANDED_CONSOLE);
}

void page_diag_build(lv_obj_t* p) {
    lv_obj_set_layout(p, LV_LAYOUT_GRID);
    static lv_coord_t c[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t r[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(p, c, r);
    lv_obj_set_style_pad_gap(p, U, 0);

    for (int i = 0; i < 6; i++) {
        lv_obj_t* b = w_button(p, DIAG[i].label, DIAG[i].cmd, C_SURFACE, C_TEXT,
                               diag_cb, (void*)DIAG[i].cmd);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
    }
}

void page_diag_tick() {}
```

- [ ] **Step 2: Write `src/ui/page_macro.cpp`**

```cpp
#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"

// FluidNC macros are named in the controller config; $RM=<name> runs one.
// Six slots is what fits at a full touch target on this row height.
static const char* MACRO_NAME[6] = { "macro0", "macro1", "macro2", "macro3", "macro4", "macro5" };

static void macro_cb(lv_event_t* e) {
    const char* name = (const char*)lv_event_get_user_data(e);
    send_linef("$RM=%s", name);
}

void page_macro_build(lv_obj_t* p) {
    lv_obj_set_layout(p, LV_LAYOUT_GRID);
    static lv_coord_t c[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t r[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(p, c, r);
    lv_obj_set_style_pad_gap(p, U, 0);

    for (int i = 0; i < 6; i++) {
        lv_obj_t* b = w_button(p, MACRO_NAME[i], "$RM", C_SURFACE, C_TEXT,
                               macro_cb, (void*)MACRO_NAME[i]);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
    }
}
```

- [ ] **Step 3: Write `src/ui/page_setup.cpp`**

```cpp
#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "hw/display_hw.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"

static lv_obj_t* s_bright_lbl;

static void bright_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int b = get_backlight() + dir * 25;
    if (b < 25)  b = 25;      // never fully dark: an unreadable panel is a hazard
    if (b > 255) b = 255;
    set_backlight((uint8_t)b);
    char t[8];
    snprintf(t, sizeof(t), "%d", b);
    lv_label_set_text(s_bright_lbl, t);
}

static void restart_ctrl_cb(lv_event_t*) { send_line("$Bye"); }

void page_setup_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(p, U, 0);

    lv_obj_t* row = w_panel(p, C_SURFACE);
    lv_obj_set_size(row, LV_PCT(100), U * 10);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, U + 4, 0);

    lv_obj_t* nm = w_label(row, "Brightness", &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_flex_grow(nm, 1);
    lv_obj_t* dn = w_button(row, "-", nullptr, C_RAISE, C_ACCENT, bright_cb, (void*)(intptr_t)-1);
    lv_obj_set_size(dn, U * 8, LV_PCT(100));
    s_bright_lbl = w_label(row, "200", &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(s_bright_lbl, U * 8);
    lv_obj_set_style_text_align(s_bright_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* up = w_button(row, "+", nullptr, C_RAISE, C_ACCENT, bright_cb, (void*)(intptr_t)1);
    lv_obj_set_size(up, U * 8, LV_PCT(100));

    lv_obj_t* rst = w_button(p, "RESTART CONTROLLER", "$Bye", C_RAISE, C_WARN,
                             restart_ctrl_cb, nullptr);
    lv_obj_set_size(rst, LV_PCT(100), U * 10);
}
```

- [ ] **Step 4: Verify it builds**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add src/ui/page_macro.cpp src/ui/page_diag.cpp src/ui/page_setup.cpp
git commit -m "ui: macro, diagnostics and setup pages"
```

---

## Task 21: Full test run and final build

- [ ] **Step 1: Run every host test**

Run: `pio test -e native`
Expected: four suites pass — `test_gcode_path` (9), `test_offsets` (8), `test_xmodem` (8),
`test_console_log` (7). 32 tests, 0 failures.

- [ ] **Step 2: Build the firmware and record the size**

Run: `pio run -e crowpanel`
Expected: `SUCCESS`, with RAM and Flash percentages printed. Flash must be well under the
3,072 KB app partition — the spec budgeted 1,462 KB free before this project started.

- [ ] **Step 3: Commit anything outstanding and push**

```bash
git add -A
git commit -m "chore: full build and test pass"
git push
```

- [ ] **Step 4: [HW] Hardware acceptance checklist**

With the panel wired to FluidNC (GPIO17→RX, GPIO18→TX, GND→GND):

1. Panel lights, nav rail visible, JOG page shown
2. State chip reads `IDLE` (not `NO LINK`)
3. DRO tracks a jog commanded from another sender
4. A jog key moves the axis by the selected step
5. **Press a jog key and slide off it — the axis stops**
6. `$MD` drops holding torque; `$ME` (or any motion) restores it
7. Home page homes all and per-axis
8. Offsets page populates from `$#`; selecting a row and Set-from-here changes it
9. Run page lists G-code files; selecting one loads a toolpath
10. Expanded view opens, FLIP swaps to console, CLOSE returns
11. Console shows traffic; the filter toggle reveals and hides `<...>` status frames
12. HOLD, RESUME and RESET work from every page

---

## Deferred

Not in this plan, recorded so they are not silently dropped:

- **MDI input row** in the console. Needs LVGL's on-screen keyboard and a numeric keypad; the
  console is read-only until then. Flagged as an open question in the spec.
- **Job progress and elapsed/remaining** on the Run page. Requires `show_file()` percent
  plumbing; the toolpath marker covers the "where am I" need first.
- **4+ axis DRO layout.** The bar is a three-column grid. Deferred until a fourth axis exists.
- **Alarm and error text from `$A`/`$E`.** Currently `$E` output lands in the console
  unparsed; caching it into a lookup is a small follow-up.
- **Macro names read from the controller.** Currently six fixed `macroN` slots.
