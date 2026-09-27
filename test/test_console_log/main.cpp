#include <unity.h>
#include "model/console_log.h"
#include <string.h>
#include <stdio.h>

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

void test_bare_codes_get_fluidnc_names() {
    clog_add("error:22");
    clog_add("error:10");      // FluidNC: soft limit (Grbl meant something else)
    clog_add("ALARM:14");
    clog_add("error:9 G-code locked out");   // already has text: untouched
    clog_add("error:999");                   // unknown: left as sent
    TEST_ASSERT_EQUAL_STRING("error:22 Gcode undefined feed rate", clog_line(0)->text);
    TEST_ASSERT_EQUAL_STRING("error:10 Soft limit error", clog_line(1)->text);
    TEST_ASSERT_EQUAL_STRING("ALARM:14 Unhomed", clog_line(2)->text);
    TEST_ASSERT_EQUAL_STRING("error:9 G-code locked out", clog_line(3)->text);
    TEST_ASSERT_EQUAL_STRING("error:999", clog_line(4)->text);
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
    clog_reset();
    (void)clog_dirty_take();
    clog_add("ok");
    TEST_ASSERT_TRUE(clog_dirty_take());
    TEST_ASSERT_FALSE(clog_dirty_take());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_status_frames_are_hidden_by_default);
    RUN_TEST(test_status_frames_appear_when_enabled);
    RUN_TEST(test_ordinary_lines_are_kept_and_classified);
    RUN_TEST(test_bare_codes_get_fluidnc_names);
    RUN_TEST(test_ring_buffer_drops_oldest_when_full);
    RUN_TEST(test_long_lines_are_truncated_not_overflowed);
    RUN_TEST(test_blank_and_null_input_is_ignored);
    RUN_TEST(test_dirty_flag_is_set_on_add_and_cleared_on_take);
    return UNITY_END();
}
