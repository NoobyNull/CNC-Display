#include <unity.h>
#include "transport/xmodem_tx.h"
#include "transport/xmodem_rx.h"
#include <string.h>
#include <vector>

// The best test available: run the transmitter straight into the receiver and
// check the file survives the round trip byte for byte.

static std::vector<uint8_t> g_wire;     // tx -> rx
static std::vector<uint8_t> g_back;     // rx -> tx
static std::vector<uint8_t> g_src;      // the "file" being sent
static size_t               g_read_pos;
static std::vector<uint8_t> g_landed;   // what the receiver delivered

static void tx_out(uint8_t c)  { g_wire.push_back(c); }
static void rx_out(uint8_t c)  { g_back.push_back(c); }
static void rx_sink(const uint8_t* d, size_t n) { g_landed.insert(g_landed.end(), d, d + n); }

static size_t src_read(uint8_t* dst, size_t max) {
    size_t n = g_src.size() - g_read_pos;
    if (n > max) n = max;
    memcpy(dst, g_src.data() + g_read_pos, n);
    g_read_pos += n;
    return n;
}

void setUp() {
    g_wire.clear(); g_back.clear(); g_landed.clear();
    g_src.clear(); g_read_pos = 0;
}
void tearDown() {}

// Pump both ends until the transfer settles.
static void run_transfer() {
    xmodem_rx_begin(rx_out, rx_sink);
    xmodem_tx_begin(tx_out, src_read);
    for (int guard = 0; guard < 200000; guard++) {
        if (!g_back.empty()) {
            uint8_t c = g_back.front();
            g_back.erase(g_back.begin());
            xmodem_tx_feed(c);
            continue;
        }
        if (!g_wire.empty()) {
            uint8_t c = g_wire.front();
            g_wire.erase(g_wire.begin());
            xmodem_rx_feed(c);
            continue;
        }
        break;
    }
}

static void fill(size_t n) {
    g_src.resize(n);
    for (size_t i = 0; i < n; i++) g_src[i] = (uint8_t)((i * 7 + 13) & 0xFF);
}

void test_single_short_block_round_trips() {
    fill(200);
    run_transfer();
    TEST_ASSERT_TRUE(xmodem_tx_done());
    TEST_ASSERT_TRUE(xmodem_tx_ok());
    TEST_ASSERT_TRUE(xmodem_rx_ok());
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(200, g_landed.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_src.data(), g_landed.data(), 200);
}

void test_exactly_one_full_block() {
    fill(1024);
    run_transfer();
    TEST_ASSERT_TRUE(xmodem_tx_ok());
    TEST_ASSERT_EQUAL_UINT32(1024, xmodem_tx_bytes());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_src.data(), g_landed.data(), 1024);
}

void test_multi_block_file_round_trips() {
    fill(5000);
    run_transfer();
    TEST_ASSERT_TRUE(xmodem_tx_ok());
    TEST_ASSERT_EQUAL_INT(5, xmodem_tx_blocks());   // 5000 -> 5 x 1K, last padded
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_src.data(), g_landed.data(), 5000);
}

void test_short_final_block_is_padded_with_ctrl_z() {
    fill(1025);
    run_transfer();
    TEST_ASSERT_TRUE(xmodem_tx_ok());
    TEST_ASSERT_EQUAL_UINT32(2048, g_landed.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_src.data(), g_landed.data(), 1025);
    TEST_ASSERT_EQUAL_UINT8(0x1A, g_landed[1025]);   // padding, receiver strips it
}

void test_empty_file_still_completes() {
    fill(0);
    run_transfer();
    TEST_ASSERT_TRUE(xmodem_tx_done());
    TEST_ASSERT_TRUE(xmodem_tx_ok());
    TEST_ASSERT_EQUAL_UINT32(0, xmodem_tx_bytes());
}

void test_cancel_from_far_end_aborts() {
    fill(4000);
    xmodem_tx_begin(tx_out, src_read);
    xmodem_tx_feed('C');
    xmodem_tx_feed(0x18);          // CAN
    TEST_ASSERT_TRUE(xmodem_tx_done());
    TEST_ASSERT_FALSE(xmodem_tx_ok());
}

void test_nak_resends_the_same_block() {
    fill(2048);
    xmodem_tx_begin(tx_out, src_read);
    xmodem_tx_feed('C');
    size_t after_first = g_wire.size();
    g_wire.clear();
    xmodem_tx_feed(0x15);          // NAK -> resend, do not advance
    TEST_ASSERT_EQUAL_UINT32(after_first, g_wire.size());
    TEST_ASSERT_EQUAL_UINT8(0x01, g_wire[1]);   // still sequence 1
    TEST_ASSERT_EQUAL_UINT32(0, xmodem_tx_bytes());
}

void test_gives_up_after_persistent_naks() {
    fill(2048);
    xmodem_tx_begin(tx_out, src_read);
    xmodem_tx_feed('C');
    for (int i = 0; i < 12; i++) xmodem_tx_feed(0x15);
    TEST_ASSERT_TRUE(xmodem_tx_done());
    TEST_ASSERT_FALSE(xmodem_tx_ok());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_single_short_block_round_trips);
    RUN_TEST(test_exactly_one_full_block);
    RUN_TEST(test_multi_block_file_round_trips);
    RUN_TEST(test_short_final_block_is_padded_with_ctrl_z);
    RUN_TEST(test_empty_file_still_completes);
    RUN_TEST(test_cancel_from_far_end_aborts);
    RUN_TEST(test_nak_resends_the_same_block);
    RUN_TEST(test_gives_up_after_persistent_naks);
    return UNITY_END();
}
