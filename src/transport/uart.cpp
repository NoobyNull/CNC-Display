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

uint32_t fnc_get_baud() { return s_baud; }

unsigned long fnc_rx_count() { return s_rx_count; }
unsigned long fnc_tx_count() { return s_tx_count; }

void init_fnc_uart() {
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
    esp_err_t e_drv = uart_driver_install(fnc_uart_port, 16384, 0, 0, NULL, 0);
    Serial.printf("[uart] num=%d tx=%d rx=%d baud=%lu cfg=%d pin=%d drv=%d\n",
                  FNC_UART_NUM, FNC_TX_PIN, FNC_RX_PIN, (unsigned long)s_baud,
                  (int)e_cfg, (int)e_pin, (int)e_drv);

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
