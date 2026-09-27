#include <unity.h>
#include "model/gcode_stats.h"

void setUp() { gstats_reset(); }
void tearDown() {}

static void feed(const char* s) { gstats_feed_line(s); }

void test_counts_lines_and_moves() {
    feed("G21 G90");
    feed("");
    feed("G0 X10 Y20");
    feed("G1 X30 Y40 F600");
    TEST_ASSERT_EQUAL_UINT32(4, gstats().lines);
    TEST_ASSERT_EQUAL_UINT32(2, gstats().moves);
}

void test_extents_come_from_cutting_moves_only() {
    feed("G21 G90");
    feed("G0 X-50 Y-50 Z20");      // rapid far away: not part of the part
    feed("G0 X0 Y0 Z5");
    feed("G1 Z-3 F300");
    feed("G1 X100 Y40");
    const gstats_t& s = gstats();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.min_x);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, s.max_x);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, s.max_y);
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, s.min_z);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, s.top_z);   // clearance counts rapids
}

void test_relative_moves_accumulate() {
    feed("G21 G91 G1 F100");
    feed("X10");
    feed("X10");
    TEST_ASSERT_EQUAL_FLOAT(20.0f, gstats().max_x);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, gstats().cut_mm);
}

void test_inches_convert_to_mm() {
    feed("G20 G90");
    feed("G1 X1 F10");
    TEST_ASSERT_TRUE(gstats().inches);
    TEST_ASSERT_EQUAL_FLOAT(25.4f, gstats().max_x);
    TEST_ASSERT_EQUAL_FLOAT(254.0f, gstats().feed_max);   // 10 in/min in mm/min
}

void test_cut_time_uses_programmed_feed() {
    feed("G21 G90 G1 F600");       // 10 mm/s
    feed("X100");                  // 100mm -> 10 s
    feed("G0 X0");                 // rapids are not in cut time
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, gstats().cut_sec);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, gstats().rapid_mm);
}

void test_arc_length_and_bulge() {
    feed("G21 G90");
    feed("G0 X10 Y0");
    feed("G3 X-10 Y0 I-10 J0 F100");   // half circle over the top, r=10
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 31.416f, gstats().cut_mm);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, gstats().max_y);   // past both endpoints
}

void test_tools_and_changes_with_descriptions() {
    feed("(T1 6mm flat endmill)");
    feed("T1 M6");
    feed("G0 X0 Y0");
    feed("M6 T2 (V-bit 60 deg)");
    feed("T1");
    feed("M06");
    const gstats_t& s = gstats();
    TEST_ASSERT_EQUAL_INT(3, s.tool_changes);
    TEST_ASSERT_EQUAL_INT(2, s.n_tools);
    TEST_ASSERT_EQUAL_INT(1, s.tools[0].num);
    TEST_ASSERT_EQUAL_STRING("T1 6mm flat endmill", s.tools[0].desc);
    TEST_ASSERT_EQUAL_INT(2, s.tools[1].num);
    TEST_ASSERT_EQUAL_STRING("V-bit 60 deg", s.tools[1].desc);
}

void test_vcarve_tool_named_by_line_after_m6() {
    feed("( Tools used in this file: )");
    feed("( 1 = End Mill {6 mm} )");
    feed("( 2 = V-Bit {60 deg 12.7 mm} )");     // header: must not name T1
    feed("G00G21G17G90G40G49G80");
    feed("G71G91.1");
    feed("T1M06");
    feed(" (End Mill {6 mm})");
    TEST_ASSERT_EQUAL_INT(1, gstats().n_tools);
    TEST_ASSERT_EQUAL_STRING("End Mill {6 mm}", gstats().tools[0].desc);
}

void test_vcarve_header_becomes_notes() {
    feed("( Toolpaths used in this file: )");
    feed("( Pocket 1 )");
    feed("( |---------------------------------------------------)");
    feed("( | Material Size)");
    feed("( | X= 600.000, Y= 400.000, Z= 18.000)");
    feed("( | Z Origin for Material  = Surface)");
    feed("G21 G90");
    feed("G0 Z5");
    feed("( Pocket 1 )");              // after the first move: not a note
    const gstats_t& s = gstats();
    TEST_ASSERT_EQUAL_INT(5, s.n_notes);   // the rule line is dropped
    TEST_ASSERT_EQUAL_STRING("Toolpaths used in this file:", s.notes[0]);
    TEST_ASSERT_EQUAL_STRING("X= 600.000, Y= 400.000, Z= 18.000", s.notes[3]);
}

void test_spindle_and_feed_range() {
    feed("G21 M3 S12000");
    feed("G1 X1 F300");
    feed("G1 X2 F1200");
    feed("S18000");
    TEST_ASSERT_EQUAL_FLOAT(300.0f, gstats().feed_min);
    TEST_ASSERT_EQUAL_FLOAT(1200.0f, gstats().feed_max);
    TEST_ASSERT_EQUAL_FLOAT(18000.0f, gstats().spindle_max);
}

void test_g91_1_is_not_relative_mode() {
    feed("G00G21G17G90G40G49G80");
    feed("G71G91.1");                  // VCarve: arc IJ incremental, not G91
    feed("G1X100.000Y50.000F1000");
    feed("G1X10.000");
    TEST_ASSERT_EQUAL_FLOAT(100.0f, gstats().max_x);   // relative would reach 110
}

void test_malformed_input_does_not_crash() {
    feed(nullptr);
    feed("X");
    feed("G1 Xabc Y");
    feed("((((");
    feed(";;;;");
    TEST_ASSERT_EQUAL_UINT32(0, gstats().moves);
    TEST_ASSERT_FALSE(gstats().have_cut);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_counts_lines_and_moves);
    RUN_TEST(test_extents_come_from_cutting_moves_only);
    RUN_TEST(test_relative_moves_accumulate);
    RUN_TEST(test_inches_convert_to_mm);
    RUN_TEST(test_cut_time_uses_programmed_feed);
    RUN_TEST(test_arc_length_and_bulge);
    RUN_TEST(test_tools_and_changes_with_descriptions);
    RUN_TEST(test_vcarve_tool_named_by_line_after_m6);
    RUN_TEST(test_vcarve_header_becomes_notes);
    RUN_TEST(test_spindle_and_feed_range);
    RUN_TEST(test_g91_1_is_not_relative_mode);
    RUN_TEST(test_malformed_input_does_not_crash);
    return UNITY_END();
}
