#include <unity.h>
#include "model/offsets.h"

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
    offsets_feed_line("ok");
    offsets_feed_line("[G54:garbage]");
    offsets_feed_line("");
    offsets_feed_line(nullptr);
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
