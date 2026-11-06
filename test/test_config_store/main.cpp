#include <unity.h>
#include <string.h>
#include "model/config_store.h"

void setUp() { cfg_clear(); }
void tearDown() {}

static void feed(const char* const* L, int n) { for (int i = 0; i < n; i++) cfg_feed_line(L[i]); }

void test_paths_follow_indentation() {
    const char* L[] = { "board: Corgi", "axes:", "  x:", "    steps_per_mm: 15.953", "    motor0:",
                        "      limit_neg_pin: gpio.1", "  y:", "    steps_per_mm: 39.882", "probe:", "  pin: NO_PIN" };
    feed(L, 10);
    TEST_ASSERT_EQUAL_STRING("15.953", cfg_get("axes/x/steps_per_mm"));
    TEST_ASSERT_EQUAL_STRING("39.882", cfg_get("axes/y/steps_per_mm"));
    TEST_ASSERT_EQUAL_STRING("gpio.1", cfg_get("axes/x/motor0/limit_neg_pin"));
    TEST_ASSERT_EQUAL_STRING("NO_PIN", cfg_get("probe/pin"));
    TEST_ASSERT_EQUAL_STRING("Corgi", cfg_get("board"));
    TEST_ASSERT_NULL(cfg_get("axes/z/steps_per_mm"));
}

void test_sibling_does_not_inherit_the_last_childs_children() {
    const char* L[] = { "a:", "  b:", "    c: 1", "  d: 2" };
    feed(L, 4);
    TEST_ASSERT_EQUAL_STRING("1", cfg_get("a/b/c"));
    TEST_ASSERT_EQUAL_STRING("2", cfg_get("a/d"));
    TEST_ASSERT_NULL(cfg_get("a/b/d"));
}

void test_comments_blanks_and_replies_are_ignored() {
    TEST_ASSERT_FALSE(cfg_feed_line(""));
    TEST_ASSERT_FALSE(cfg_feed_line("  # a comment: with a colon"));
    TEST_ASSERT_FALSE(cfg_feed_line("ok"));
    TEST_ASSERT_FALSE(cfg_feed_line("error:3"));
    TEST_ASSERT_FALSE(cfg_feed_line("[MSG:INFO: x]"));
    TEST_ASSERT_EQUAL(0, cfg_count());
}

void test_trailing_comment_and_float() {
    cfg_feed_line("x_rate: 6000   # mm/min");
    TEST_ASSERT_EQUAL_STRING("6000", cfg_get("x_rate"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6000.0f, cfg_float("x_rate", 0));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.0f, cfg_float("missing", 7.0f));
}

void test_clear_forgets_everything_and_bumps_generation() {
    cfg_feed_line("a: 1");
    uint32_t g = cfg_generation();
    cfg_clear();
    TEST_ASSERT_NULL(cfg_get("a"));
    TEST_ASSERT_TRUE(cfg_generation() != g);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_paths_follow_indentation);
    RUN_TEST(test_sibling_does_not_inherit_the_last_childs_children);
    RUN_TEST(test_comments_blanks_and_replies_are_ignored);
    RUN_TEST(test_trailing_comment_and_float);
    RUN_TEST(test_clear_forgets_everything_and_bumps_generation);
    return UNITY_END();
}
