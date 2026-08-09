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

void fnc_set_baud(uint32_t baud) {
    s_baud = baud;
    uart_set_baudrate(fnc_uart_port, baud);
    uart_flush_input(fnc_uart_port);   // whatever arrived at the old rate is noise
}

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

    uart_param_config(fnc_uart_port, &conf);
    uart_set_pin(fnc_uart_port, (gpio_num_t)FNC_TX_PIN, (gpio_num_t)FNC_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(fnc_uart_port, 4096, 0, 0, NULL, 0);
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
