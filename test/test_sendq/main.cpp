#include <unity.h>
#include <stdio.h>
#include <string.h>

#include "model/sendq.h"

static sendq_t q;

void setUp() { sendq_reset(&q); }
void tearDown() {}

static void push_n(int n) {
    char b[16];
    for (int i = 0; i < n; i++) {
        snprintf(b, sizeof(b), "L%d", i);
        sendq_push(&q, b);
    }
}

static void expect_pop(const char* want) {
    char out[SENDQ_LINE];
    TEST_ASSERT_TRUE(sendq_pop(&q, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(want, out);
}

void test_empty_pop_fails() {
    char out[SENDQ_LINE];
    TEST_ASSERT_FALSE(sendq_pop(&q, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, sendq_count(&q));
}

// Order is the whole point: page_offsets sends "G10 L20 ..." then "$#", and
// page_cal sends "G91 G0 ..." then "G90". Reordering those changes what the
// machine does.
void test_fifo_order() {
    sendq_push(&q, "G10 L20 P1 X0");
    sendq_push(&q, "$#");
    TEST_ASSERT_EQUAL_UINT(2, sendq_count(&q));
    expect_pop("G10 L20 P1 X0");
    expect_pop("$#");
    TEST_ASSERT_EQUAL_UINT(0, sendq_count(&q));
}

// Push and pop past the end of the array many times over.
void test_wraparound() {
    char b[16];
    for (int round = 0; round < SENDQ_SLOTS * 3; round++) {
        snprintf(b, sizeof(b), "R%d", round);
        TEST_ASSERT_TRUE(sendq_push(&q, b));
        expect_pop(b);
    }
    TEST_ASSERT_EQUAL_UINT(0, sendq_count(&q));
}

void test_fills_exactly_to_capacity() {
    push_n(SENDQ_SLOTS);
    TEST_ASSERT_EQUAL_UINT(SENDQ_SLOTS, sendq_count(&q));
    TEST_ASSERT_EQUAL_UINT32(0, sendq_dropped(&q));
    expect_pop("L0");
}

// A full queue drops the NEWEST and says so. Dropping silently would look like
// the controller ignoring a command.
void test_overflow_drops_newest_and_counts() {
    push_n(SENDQ_SLOTS);
    TEST_ASSERT_FALSE(sendq_push(&q, "TOO_MUCH"));
    TEST_ASSERT_EQUAL_UINT32(1, sendq_dropped(&q));
    TEST_ASSERT_EQUAL_UINT(SENDQ_SLOTS, sendq_count(&q));
    expect_pop("L0");   // oldest survived
}

// Room reappears once a line goes out.
void test_recovers_after_pop() {
    push_n(SENDQ_SLOTS);
    char out[SENDQ_LINE];
    TEST_ASSERT_TRUE(sendq_pop(&q, out, sizeof(out)));
    TEST_ASSERT_TRUE(sendq_push(&q, "AFTER"));
    for (int i = 1; i < SENDQ_SLOTS; i++) TEST_ASSERT_TRUE(sendq_pop(&q, out, sizeof(out)));
    expect_pop("AFTER");
}

// Truncation must not run off the end of the slot.
void test_long_line_truncates_and_terminates() {
    char big[SENDQ_LINE * 2];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    TEST_ASSERT_TRUE(sendq_push(&q, big));
    char out[SENDQ_LINE];
    TEST_ASSERT_TRUE(sendq_pop(&q, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(SENDQ_LINE - 1, strlen(out));
}

void test_null_push_is_rejected() {
    TEST_ASSERT_FALSE(sendq_push(&q, nullptr));
    TEST_ASSERT_EQUAL_UINT(0, sendq_count(&q));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_pop_fails);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_fills_exactly_to_capacity);
    RUN_TEST(test_overflow_drops_newest_and_counts);
    RUN_TEST(test_recovers_after_pop);
    RUN_TEST(test_long_line_truncates_and_terminates);
    RUN_TEST(test_null_push_is_rejected);
    return UNITY_END();
}
