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
