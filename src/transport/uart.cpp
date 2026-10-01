#include "uart.h"
#include <Arduino.h>
#include <driver/uart.h>

// Lifted from FluidDial-main/src/SystemArduino.cpp:133. UART_SCLK_XTAL is what
// keeps 1 Mbaud stable on the S3 when the APB clock scales — do not "simplify"
// it back to the default clock source.

static uart_port_t fnc_uart_port = (uart_port_t)FNC_UART_NUM;

static uint32_t      s_baud = FNC_BAUD;
static unsigned long s_rx_count = 0;
static unsigned long s_tx_count = 0;

// Why the peripheral is silent, for the Setup page. Empty means init was clean.
// Serial shares this link's pins and is un-begun whenever the link owns them
// (main.cpp:46), so every printf below reaches nobody in the one configuration
// where these errors matter — the glass is the only place they can land.
static char s_init_err[32] = "";

// Receive-side losses the driver can see: the 128-byte hardware FIFO
// overflowing (the RX interrupt held off too long), the 16K ring filling, and
// framing / parity errors (a baud mismatch or a noisy line). Counted from the
// driver's event queue by uart_poll_events(); nonzero means bytes were lost
// before the parser ever saw them.
static QueueHandle_t s_evq = nullptr;
static uart_rx_losses_t s_loss = {};

const char* fnc_init_error() { return s_init_err; }

uint32_t fnc_get_baud() { return s_baud; }

unsigned long fnc_rx_count() { return s_rx_count; }
unsigned long fnc_tx_count() { return s_tx_count; }

bool init_fnc_uart() {
    fnc_uart_port = (uart_port_t)FNC_UART_NUM;
    uart_driver_delete(fnc_uart_port);

    uart_config_t conf = {};
    conf.baud_rate           = s_baud;
    conf.data_bits           = UART_DATA_8_BITS;
    conf.parity              = UART_PARITY_DISABLE;
    conf.stop_bits           = UART_STOP_BITS_1;
    conf.flow_ctrl           = UART_HW_FLOWCTRL_DISABLE;
    conf.rx_flow_ctrl_thresh = 0;
    conf.source_clk          = UART_SCLK_XTAL;  // independent of APB frequency

    esp_err_t e_cfg = uart_param_config(fnc_uart_port, &conf);
    esp_err_t e_pin = uart_set_pin(fnc_uart_port, (gpio_num_t)FNC_TX_PIN, (gpio_num_t)FNC_RX_PIN,
                                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    // 16K RX: a $S dump streams ~150 lines at 1 Mbaud (~100 bytes/ms) and the
    // main loop drains between LVGL frames — 4K overflowed mid-dump and the
    // shredded lines made the settings list come up with a random subset.
    esp_err_t e_drv = uart_driver_install(fnc_uart_port, 16384, 0, 32, &s_evq, 0);
    Serial.printf("[uart] num=%d tx=%d rx=%d baud=%lu cfg=%d pin=%d drv=%d\n",
                  FNC_UART_NUM, FNC_TX_PIN, FNC_RX_PIN, (unsigned long)s_baud,
                  (int)e_cfg, (int)e_pin, (int)e_drv);

    // A driver that failed to install must never read as a ready link: every
    // read and write after it returns an error, which looks exactly like a
    // dead cable. 16K of RX ring comes out of internal DRAM, and this board
    // spends most of it on LVGL.
    if (e_cfg || e_pin || e_drv) {
        snprintf(s_init_err, sizeof(s_init_err), "cfg%d pin%d drv%d",
                 (int)e_cfg, (int)e_pin, (int)e_drv);
        return false;
    }

    // Silicon-internal loopback: TX routed to RX inside the chip, no pads
    // involved. Proves the peripheral + driver actually transmit.
    uart_set_loop_back(fnc_uart_port, true);
    uart_flush_input(fnc_uart_port);
    uart_write_bytes(fnc_uart_port, "PING", 4);
    uart_wait_tx_done(fnc_uart_port, pdMS_TO_TICKS(100));
    uint8_t rb[5] = {0};
    int n = uart_read_bytes(fnc_uart_port, rb, 4, pdMS_TO_TICKS(100));
    uart_set_loop_back(fnc_uart_port, false);
    Serial.printf("[uart] internal loopback: n=%d data=%s\n", n, (const char*)rb);

    // Short of 4 bytes means the peripheral itself is not transmitting — no
    // cable can explain that. Reported, not fatal: refusing the link on it
    // would turn a flaky self-test into a dead pendant.
    if (n == 4) s_init_err[0] = 0;
    else        snprintf(s_init_err, sizeof(s_init_err), "lb%d/4", n);
    return true;
}

void reinit_fnc_uart() {
    init_fnc_uart();
}

void uart_putc(uint8_t c) {
    uart_write_bytes(fnc_uart_port, (const char*)&c, 1);
    s_tx_count++;
}

int uart_getc() {
    uint8_t c;
    int n = uart_read_bytes(fnc_uart_port, &c, 1, 0);
    if (n != 1) return -1;
    s_rx_count++;
    return c;
}

bool uart_rx_waiting() {
    size_t avail = 0;
    uart_get_buffered_data_len(fnc_uart_port, &avail);
    return avail > 0;
}

// Clear a stale XOFF on the far end. FluidDial also calls uart_ll_force_xon()
// to unwedge the local transmitter, but that only matters when software flow
// control is enabled on this side — we configure UART_HW_FLOWCTRL_DISABLE and
// never turn SW flow control on, so the local transmitter cannot be held off.
// Sending XON to FluidNC is the half that does the work.
void resetFlowControl() {
    uart_putc(0x11);
}

// GrblParserC expects the host to supply a millisecond clock.
extern "C" int milliseconds() {
    return (int)millis();
}

bool uart_poll_events() {
    if (!s_evq) return false;
    bool lost = false;
    uart_event_t ev;
    while (xQueueReceive(s_evq, &ev, 0) == pdTRUE) {
        switch (ev.type) {
            case UART_FIFO_OVF:    s_loss.fifo_ovf++;  lost = true; break;
            case UART_BUFFER_FULL: s_loss.buf_full++;  lost = true; break;
            case UART_FRAME_ERR:   s_loss.frame_err++; lost = true; break;
            case UART_PARITY_ERR:  s_loss.parity_err++; lost = true; break;
            default: break;   // UART_DATA etc.: the ring already has the bytes
        }
    }
    return lost;
}

const uart_rx_losses_t& uart_rx_losses() { return s_loss; }
