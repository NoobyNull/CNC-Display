#include <unity.h>
#include "transport/xmodem_rx.h"
#include <string.h>
#include <stdio.h>
#include <vector>

static std::vector<uint8_t> g_tx;     // bytes the receiver sent back
static std::vector<uint8_t> g_sink;   // bytes handed to the consumer

static void tx_cb(uint8_t c) { g_tx.push_back(c); }
static void sink_cb(const uint8_t* d, size_t n) { g_sink.insert(g_sink.end(), d, d + n); }

void setUp() {
    g_tx.clear();
    g_sink.clear();
    xmodem_rx_begin(tx_cb, sink_cb);
}
void tearDown() {}

// CRC16-CCITT, the polynomial FluidNC's xmodem.cpp uses.
static uint16_t crc16(const uint8_t* d, size_t n) {
    uint16_t c = 0;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (int k = 0; k < 8; k++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

static void send_block(uint8_t seq, const uint8_t* payload, size_t size, bool corrupt = false) {
    std::vector<uint8_t> b;
    b.push_back(size == 1024 ? 0x02 : 0x01);
    b.push_back(seq);
    b.push_back((uint8_t)~seq);
    b.insert(b.end(), payload, payload + size);
    uint16_t c = crc16(payload, size);
    if (corrupt) c ^= 0xFFFF;
    b.push_back((uint8_t)(c >> 8));
    b.push_back((uint8_t)(c & 0xFF));
    for (uint8_t x : b) xmodem_rx_feed(x);
}

void test_begin_requests_crc_mode() {
    TEST_ASSERT_EQUAL_UINT32(1, g_tx.size());
    TEST_ASSERT_EQUAL_UINT8('C', g_tx[0]);
}

void test_good_1k_block_is_acked_and_delivered() {
    uint8_t p[1024];
    for (int i = 0; i < 1024; i++) p[i] = (uint8_t)(i & 0xFF);
    send_block(1, p, 1024);
    TEST_ASSERT_EQUAL_UINT8(0x06, g_tx.back());
    TEST_ASSERT_EQUAL_UINT32(1024, g_sink.size());
    TEST_ASSERT_EQUAL_UINT8(0x00, g_sink[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, g_sink[255]);
}

void test_128_byte_block_is_accepted_too() {
    uint8_t p[128];
    memset(p, 'A', sizeof(p));
    send_block(1, p, 128);
    TEST_ASSERT_EQUAL_UINT8(0x06, g_tx.back());
    TEST_ASSERT_EQUAL_UINT32(128, g_sink.size());
}

void test_bad_crc_is_nakked_and_not_delivered() {
    uint8_t p[128];
    memset(p, 'B', sizeof(p));
    send_block(1, p, 128, /*corrupt=*/true);
    TEST_ASSERT_EQUAL_UINT8(0x15, g_tx.back());
    TEST_ASSERT_EQUAL_UINT32(0, g_sink.size());
}

void test_duplicate_block_is_acked_but_not_delivered_twice() {
    uint8_t p[128];
    memset(p, 'C', sizeof(p));
    send_block(1, p, 128);
    send_block(1, p, 128);
    TEST_ASSERT_EQUAL_UINT8(0x06, g_tx.back());
    TEST_ASSERT_EQUAL_UINT32(128, g_sink.size());
}

void test_two_sequential_blocks_deliver_in_order() {
    uint8_t a[128], b[128];
    memset(a, 'a', sizeof(a));
    memset(b, 'b', sizeof(b));
    send_block(1, a, 128);
    send_block(2, b, 128);
    TEST_ASSERT_EQUAL_UINT32(256, g_sink.size());
    TEST_ASSERT_EQUAL_UINT8('a', g_sink[0]);
    TEST_ASSERT_EQUAL_UINT8('b', g_sink[128]);
}

void test_eot_completes_the_transfer() {
    uint8_t p[128];
    memset(p, 'D', sizeof(p));
    send_block(1, p, 128);
    TEST_ASSERT_FALSE(xmodem_rx_done());
    xmodem_rx_feed(0x04);
    TEST_ASSERT_TRUE(xmodem_rx_done());
    TEST_ASSERT_TRUE(xmodem_rx_ok());
    TEST_ASSERT_EQUAL_UINT8(0x06, g_tx.back());
}

void test_cancel_sequence_aborts() {
    xmodem_rx_feed(0x18);
    xmodem_rx_feed(0x18);
    TEST_ASSERT_TRUE(xmodem_rx_done());
    TEST_ASSERT_FALSE(xmodem_rx_ok());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_begin_requests_crc_mode);
    RUN_TEST(test_good_1k_block_is_acked_and_delivered);
    RUN_TEST(test_128_byte_block_is_accepted_too);
    RUN_TEST(test_bad_crc_is_nakked_and_not_delivered);
    RUN_TEST(test_duplicate_block_is_acked_but_not_delivered_twice);
    RUN_TEST(test_two_sequential_blocks_deliver_in_order);
    RUN_TEST(test_eot_completes_the_transfer);
    RUN_TEST(test_cancel_sequence_aborts);
    return UNITY_END();
}
