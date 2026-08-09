#include "uart.h"
#include <Arduino.h>
#include <driver/uart.h>

// Lifted from FluidDial-main/src/SystemArduino.cpp:133. UART_SCLK_XTAL is what
// keeps 1 Mbaud stable on the S3 when the APB clock scales — do not "simplify"
// it back to the default clock source.

static uart_port_t fnc_uart_port = (uart_port_t)FNC_UART_NUM;

void init_fnc_uart() {
    fnc_uart_port = (uart_port_t)FNC_UART_NUM;
    uart_driver_delete(fnc_uart_port);

    uart_config_t conf = {};
    conf.baud_rate           = FNC_BAUD;
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

extern "C" void fnc_putchar(uint8_t c) {
    uart_write_bytes(fnc_uart_port, (const char*)&c, 1);
}

extern "C" int fnc_getchar() {
    uint8_t c;
    int n = uart_read_bytes(fnc_uart_port, &c, 1, 0);
    return n == 1 ? c : -1;
}

extern "C" bool fnc_rx_waiting() {
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
    fnc_putchar(0x11);
}
