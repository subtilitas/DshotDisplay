// Host-test stub for hardware/uart.h. Never readable: the host tests have no
// ESC to answer a telemetry request.
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct uart_inst uart_inst_t;
extern uart_inst_t *uart0;
extern uart_inst_t *uart1;
enum uart_parity_t { UART_PARITY_NONE = 0, UART_PARITY_EVEN, UART_PARITY_ODD };
unsigned uart_init(uart_inst_t *u, unsigned baud);
void uart_deinit(uart_inst_t *u);
bool uart_is_readable(uart_inst_t *u);
uint8_t uart_getc(uart_inst_t *u);
void uart_set_format(uart_inst_t *u, unsigned bits, unsigned stop, enum uart_parity_t p);
void uart_set_fifo_enabled(uart_inst_t *u, bool on);
