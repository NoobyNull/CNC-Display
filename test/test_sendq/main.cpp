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

static void expect_pop_seq(const char* want, sendq_seq_t want_seq) {
    char out[SENDQ_LINE];
    sendq_seq_t seq = 99;
    TEST_ASSERT_TRUE(sendq_pop_seq(&q, out, sizeof(out), &seq));
    TEST_ASSERT_EQUAL_STRING(want, out);
    TEST_ASSERT_EQUAL_UINT16(want_seq, seq);
}

// AutoZero is 28 lines. It used to go out one send_line() at a time into 16
// slots, so the last 11 -- the Y walls, the G10 and the lift out of the pocket
// -- were dropped and the bit stayed in the plate.
void test_autozero_sized_sequence_fits_an_empty_queue() {
    const char* lines[28];
    for (int i = 0; i < 28; i++) lines[i] = "G38.2 Z-5 F75";
    TEST_ASSERT_TRUE(sendq_push_all(&q, lines, 28, 1));
    TEST_ASSERT_EQUAL_UINT(28, sendq_count(&q));
}

void test_sequence_is_all_or_nothing() {
    push_n(SENDQ_SLOTS - 2);
    const char* lines[] = { "G91", "G0 X1", "G90" };
    TEST_ASSERT_FALSE(sendq_push_all(&q, lines, 3, 7));
    TEST_ASSERT_EQUAL_UINT(SENDQ_SLOTS - 2, sendq_count(&q));   // nothing half-queued
    TEST_ASSERT_EQUAL_UINT32(0, sendq_dropped(&q));             // refused, not dropped
    char out[SENDQ_LINE];
    TEST_ASSERT_TRUE(sendq_pop(&q, out, sizeof(out)));
    TEST_ASSERT_TRUE(sendq_push_all(&q, lines, 3, 7));
}

void test_sequence_rejects_seq_zero_and_null_lines() {
    const char* lines[] = { "G91", nullptr };
    TEST_ASSERT_FALSE(sendq_push_all(&q, lines, 1, 0));
    TEST_ASSERT_FALSE(sendq_push_all(&q, lines, 2, 3));
    TEST_ASSERT_EQUAL_UINT(0, sendq_count(&q));
}

void test_pop_reports_sequence() {
    sendq_push(&q, "$#");
    const char* lines[] = { "G91", "G90" };
    sendq_push_all(&q, lines, 2, 5);
    expect_pop_seq("$#", 0);
    expect_pop_seq("G91", 5);
    expect_pop_seq("G90", 5);
}

// An error inside a sequence drops the rest of THAT sequence; plain lines and
// other sequences around it keep their order.
void test_drop_one_sequence_keeps_the_rest_in_order() {
    const char* a[] = { "A1", "A2" };
    const char* b[] = { "B1", "B2" };
    sendq_push(&q, "P1");
    sendq_push_all(&q, a, 2, 1);
    sendq_push(&q, "P2");
    sendq_push_all(&q, b, 2, 2);
    TEST_ASSERT_EQUAL_INT(2, sendq_drop_seq(&q, 1));
    expect_pop_seq("P1", 0);
    expect_pop_seq("P2", 0);
    expect_pop_seq("B1", 2);
    expect_pop_seq("B2", 2);
    TEST_ASSERT_EQUAL_UINT(0, sendq_count(&q));
}

void test_drop_all_sequences_keeps_plain_lines() {
    const char* a[] = { "A1", "A2" };
    sendq_push_all(&q, a, 2, 1);
    sendq_push(&q, "$A");
    TEST_ASSERT_EQUAL_INT(2, sendq_drop_all_seqs(&q));
    expect_pop_seq("$A", 0);
}

// Compaction across the end of the ring.
void test_drop_across_wraparound() {
    char out[SENDQ_LINE];
    push_n(SENDQ_SLOTS - 3);
    for (int i = 0; i < SENDQ_SLOTS - 3; i++) sendq_pop(&q, out, sizeof(out));
    const char* a[] = { "A1", "A2", "A3", "A4", "A5" };
    sendq_push(&q, "P1");
    sendq_push_all(&q, a, 5, 9);
    sendq_push(&q, "P2");
    TEST_ASSERT_EQUAL_INT(5, sendq_drop_seq(&q, 9));
    expect_pop_seq("P1", 0);
    expect_pop_seq("P2", 0);
    TEST_ASSERT_EQUAL_UINT(0, sendq_count(&q));
}

void test_drop_seq_zero_is_a_no_op() {
    sendq_push(&q, "P1");
    TEST_ASSERT_EQUAL_INT(0, sendq_drop_seq(&q, 0));
    TEST_ASSERT_EQUAL_UINT(1, sendq_count(&q));
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
    RUN_TEST(test_autozero_sized_sequence_fits_an_empty_queue);
    RUN_TEST(test_sequence_is_all_or_nothing);
    RUN_TEST(test_sequence_rejects_seq_zero_and_null_lines);
    RUN_TEST(test_pop_reports_sequence);
    RUN_TEST(test_drop_one_sequence_keeps_the_rest_in_order);
    RUN_TEST(test_drop_all_sequences_keeps_plain_lines);
    RUN_TEST(test_drop_across_wraparound);
    RUN_TEST(test_drop_seq_zero_is_a_no_op);
    return UNITY_END();
}
