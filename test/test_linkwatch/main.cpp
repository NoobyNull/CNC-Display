#include <unity.h>
#include "model/linkwatch.h"

void setUp() { linkwatch_reset(); }
void tearDown() {}

// Run the watch from t0 to t1 in 10 ms steps with a fixed counter, counting
// what it asked for.
static int pings, losts;
static void run(uint32_t t0, uint32_t t1, unsigned long rx, bool connected = true) {
    pings = losts = 0;
    for (uint32_t t = t0; t <= t1; t += 10) {
        linkwatch_t a = linkwatch_tick(t, rx, connected);
        if (a == LINKWATCH_PING) pings++;
        if (a == LINKWATCH_LOST) losts++;
    }
}

void test_traffic_keeps_it_quiet() {
    for (uint32_t t = 0; t < 20000; t += 10) {
        TEST_ASSERT_EQUAL(LINKWATCH_OK, linkwatch_tick(t, t / 500, true));   // a byte every 0.5 s
    }
}

void test_silence_pings_then_declares_loss() {
    run(0, 3990, 100);
    TEST_ASSERT_EQUAL(3, pings);   // at 1, 2 and 3 s
    TEST_ASSERT_EQUAL(0, losts);
    TEST_ASSERT_EQUAL(LINKWATCH_LOST, linkwatch_tick(4000, 100, true));
}

// The caller marks the link down on LOST; from then on nothing more is said
// until it comes back.
void test_loss_is_reported_once() {
    run(0, 4000, 100);
    TEST_ASSERT_EQUAL(1, losts);
    run(4010, 20000, 100, false);
    TEST_ASSERT_EQUAL(0, losts);
}

void test_a_reply_to_the_ping_resets_the_window() {
    run(0, 1500, 100);
    TEST_ASSERT_EQUAL(1, pings);
    run(1510, 5000, 101);   // the controller answered at 1.51 s
    TEST_ASSERT_EQUAL(0, losts);
}

// While the panel already shows NO LINK there is nothing to lose, and coming
// back up must not inherit the old silence.
void test_disconnected_never_reports_loss() {
    run(0, 20000, 100, false);
    TEST_ASSERT_EQUAL(0, losts);
    TEST_ASSERT_EQUAL(0, pings);
    run(20010, 23990, 100, true);   // up again: a full window before any verdict
    TEST_ASSERT_EQUAL(0, losts);
}

void test_clock_wraparound() {
    const uint32_t t0 = 0xFFFFF000u;
    TEST_ASSERT_EQUAL(LINKWATCH_OK, linkwatch_tick(t0, 5, true));
    TEST_ASSERT_EQUAL(LINKWATCH_LOST, linkwatch_tick(t0 + 4000, 5, true));   // wraps past zero
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_traffic_keeps_it_quiet);
    RUN_TEST(test_silence_pings_then_declares_loss);
    RUN_TEST(test_loss_is_reported_once);
    RUN_TEST(test_a_reply_to_the_ping_resets_the_window);
    RUN_TEST(test_disconnected_never_reports_loss);
    RUN_TEST(test_clock_wraparound);
    return UNITY_END();
}
