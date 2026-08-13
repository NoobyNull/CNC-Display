#include <unity.h>

#include "model/corner_gesture.h"

// The real panel. Corner boxes are a fifth of each dimension: x<204 or x>=820,
// y<120 or y>=480.
#define W 1024
#define H 600

static cg_state_t g;

void setUp() { cg_init(&g, W, H); }
void tearDown() {}

static const cg_point_t FOUR[4] = {
    {   10,  10 },   // top-left
    { 1010,  10 },   // top-right
    {   10, 590 },   // bottom-left
    { 1010, 590 },   // bottom-right
};

void test_four_corners_recognised() {
    TEST_ASSERT_TRUE(cg_corners_held(&g, FOUR, 4));
}

void test_three_corners_is_not_enough() {
    TEST_ASSERT_FALSE(cg_corners_held(&g, FOUR, 3));
}

// Four fingers bunched together — a palm, or a sleeve — must never count, no
// matter how long they rest there.
void test_four_fingers_in_one_corner_never_fires() {
    const cg_point_t clump[4] = { {10,10}, {30,10}, {10,30}, {30,30} };
    TEST_ASSERT_FALSE(cg_corners_held(&g, clump, 4));
    TEST_ASSERT_FALSE(cg_update(&g, clump, 4, 1000));
    TEST_ASSERT_FALSE(cg_update(&g, clump, 4, 1000 + CG_HOLD_MS * 5));
}

// Dead centre is in no corner box.
void test_centre_is_not_a_corner() {
    const cg_point_t mid[4] = { {512,300}, {500,300}, {512,310}, {520,290} };
    TEST_ASSERT_FALSE(cg_corners_held(&g, mid, 4));
}

void test_fires_only_after_the_hold() {
    TEST_ASSERT_FALSE(cg_update(&g, FOUR, 4, 1000));                    // lands
    TEST_ASSERT_FALSE(cg_update(&g, FOUR, 4, 1000 + CG_HOLD_MS - 1));   // not yet
    TEST_ASSERT_TRUE(cg_update(&g, FOUR, 4, 1000 + CG_HOLD_MS));        // fires
}

// Once per gesture, not once per poll: at 10Hz a latch-free version would open
// the console dozens of times while the fingers stayed down.
void test_fires_exactly_once_while_held() {
    cg_update(&g, FOUR, 4, 1000);
    TEST_ASSERT_TRUE(cg_update(&g, FOUR, 4, 1000 + CG_HOLD_MS));
    for (uint32_t t = 1; t < 20; t++) {
        TEST_ASSERT_FALSE(cg_update(&g, FOUR, 4, 1000 + CG_HOLD_MS + t * 100));
    }
}

void test_lifting_rearms_for_a_second_gesture() {
    cg_update(&g, FOUR, 4, 1000);
    TEST_ASSERT_TRUE(cg_update(&g, FOUR, 4, 1000 + CG_HOLD_MS));
    cg_update(&g, nullptr, 0, 5000);                       // fingers lift
    TEST_ASSERT_FALSE(cg_update(&g, FOUR, 4, 6000));       // land again
    TEST_ASSERT_TRUE(cg_update(&g, FOUR, 4, 6000 + CG_HOLD_MS));
}

// Letting go of one corner part-way through restarts the clock; the hold has to
// be continuous.
void test_breaking_the_hold_restarts_the_clock() {
    cg_update(&g, FOUR, 4, 1000);
    cg_update(&g, FOUR, 3, 1500);                           // one finger leaves
    TEST_ASSERT_FALSE(cg_update(&g, FOUR, 4, 2000));        // clock restarts here
    TEST_ASSERT_FALSE(cg_update(&g, FOUR, 4, 2000 + CG_HOLD_MS - 1));
    TEST_ASSERT_TRUE(cg_update(&g, FOUR, 4, 2000 + CG_HOLD_MS));
}

// millis() really is 0 for the first millisecond after boot, and 0 is the
// "not held" sentinel.
void test_hold_starting_at_time_zero() {
    TEST_ASSERT_FALSE(cg_update(&g, FOUR, 0 == 0 ? 4 : 4, 0));
    TEST_ASSERT_TRUE(cg_update(&g, FOUR, 4, CG_HOLD_MS + 1));
}

// A fifth contact resting elsewhere should not spoil the gesture.
void test_extra_contact_is_ignored() {
    const cg_point_t five[5] = {
        {10,10}, {1010,10}, {10,590}, {1010,590}, {512,300}
    };
    TEST_ASSERT_TRUE(cg_corners_held(&g, five, 5));
}

void test_no_points_is_not_held() {
    TEST_ASSERT_FALSE(cg_corners_held(&g, nullptr, 0));
    TEST_ASSERT_FALSE(cg_corners_held(&g, FOUR, 0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_four_corners_recognised);
    RUN_TEST(test_three_corners_is_not_enough);
    RUN_TEST(test_four_fingers_in_one_corner_never_fires);
    RUN_TEST(test_centre_is_not_a_corner);
    RUN_TEST(test_fires_only_after_the_hold);
    RUN_TEST(test_fires_exactly_once_while_held);
    RUN_TEST(test_lifting_rearms_for_a_second_gesture);
    RUN_TEST(test_breaking_the_hold_restarts_the_clock);
    RUN_TEST(test_hold_starting_at_time_zero);
    RUN_TEST(test_extra_contact_is_ignored);
    RUN_TEST(test_no_points_is_not_held);
    return UNITY_END();
}
