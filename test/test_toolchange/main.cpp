#include <unity.h>
#include "model/toolchange.h"

void setUp() { tc_clear(); }
void tearDown() {}

void test_post_message_names_the_tool() {
    tc_note_message(" MSG, Change to T12: 12 SpeTool Ball Nose 1/64\" Radius");
    TEST_ASSERT_EQUAL(12, tc_tool());
    TEST_ASSERT_EQUAL_STRING("12 SpeTool Ball Nose 1/64\" Radius", tc_name());
    TEST_ASSERT_FALSE(tc_measured());
}

void test_old_post_text_is_trimmed() {
    tc_note_message(" MSG, Load T1: 1 #45525 SPOILBOARD   - zero Z, then Resume");
    TEST_ASSERT_EQUAL(1, tc_tool());
    TEST_ASSERT_EQUAL_STRING("1 #45525 SPOILBOARD", tc_name());
}

void test_atc_install_keeps_the_name_and_marks_measured() {
    tc_note_message(" MSG, Change to T2: 2 HOG Roughing End Mill");
    tc_note_message(" MSG, Install tool #2 then resume to continue");
    TEST_ASSERT_EQUAL(2, tc_tool());
    TEST_ASSERT_EQUAL_STRING("2 HOG Roughing End Mill", tc_name());
    TEST_ASSERT_TRUE(tc_measured());
}

void test_other_messages_ignored() {
    tc_note_message("uart_channel2 auto report interval set to 200 ms");
    tc_note_message(" MSG, TLO Z reset to 0");
    TEST_ASSERT_EQUAL(0, tc_tool());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_post_message_names_the_tool);
    RUN_TEST(test_old_post_text_is_trimmed);
    RUN_TEST(test_atc_install_keeps_the_name_and_marks_measured);
    RUN_TEST(test_other_messages_ignored);
    return UNITY_END();
}
