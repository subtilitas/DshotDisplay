/**
 * @file pio_uart_rx.cpp
 * @brief The RP2350 side of @ref pio_uart_rx.h: claim a state machine, point it
 *        at a pin, read bytes out of its FIFO.
 *
 * Device-only. The program it loads is in pio_uart_rx.pio, assembled into a
 * header at build time by `pico_generate_pio_header()`; test/stubs carries a
 * stand-in for that header so `make syntax` can still typecheck this file.
 */

#include "pio_uart_rx.h"
#include "pio_uart_rx.pio.h"

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>

/** @brief State machine cycles per bit. Fixed by the program. */
#define PIO_UART_RX_CYCLES_PER_BIT 8u

static PIO     s_pio    = nullptr; /**< Block the program was loaded into. */
static uint    s_sm     = 0;       /**< State machine within it. */
static uint    s_offset = 0;       /**< Where the program landed. */
static uint8_t s_pin    = 0;       /**< GPIO being sampled. */
static bool    s_active = false;   /**< True while all of the above mean something. */

bool pioUartRxBegin(uint8_t pin, uint32_t baud) {
	pioUartRxEnd();
	if (pin > 29 || baud == 0) return false;

	// The block and state machine are whatever is free, never a constant. Two
	// other programs are already resident on a running board — bidirectional
	// DShot always, the SDIO card driver on the 2.8" — and which instruction
	// memory each of them landed in is not something this file can know.
	// Asking also settles the RP2350's per-block GPIO base, which a hardcoded
	// block would have to get right by hand for a pin the user picked at run
	// time.
	if (!pio_claim_free_sm_and_add_program_for_gpio_range(
	            &pio_uart_rx_program, &s_pio, &s_sm, &s_offset, pin, 1, true))
		return false;

	pio_sm_config c = pio_uart_rx_program_get_default_config(s_offset);
	sm_config_set_in_pins(&c, pin);   // IN, and the WAIT that finds the start bit
	sm_config_set_jmp_pin(&c, pin);   // the stop-bit test

	// Shift right, no autopush: a UART byte arrives least significant bit
	// first, so shifting right assembles it the right way round and leaves it
	// in the top eight bits of a 32-bit ISR. That is why pioUartRxGetc() reads
	// bits 31..24 rather than the low byte.
	sm_config_set_in_shift(&c, true, false, 32);

	// Nothing is ever transmitted, so the TX half of the FIFO is dead weight.
	// Joined onto the RX side it buys four more bytes of slack, which at 115200
	// baud is about 350 us — worth having, and still nothing like the PL011's
	// thirty-two. @see pio_uart_rx_cost
	sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

	sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) /
	                         (float)(baud * PIO_UART_RX_CYCLES_PER_BIT));

	pio_gpio_init(s_pio, pin);
	// The ESC drives this line; the pull-up is for when it does not. A floating
	// input decodes noise into the frame assembler, and a decoder reporting bad
	// frames is much harder to read as "nothing is plugged in" than silence is.
	gpio_pull_up(pin);
	pio_sm_set_consecutive_pindirs(s_pio, s_sm, pin, 1, false);

	pio_sm_init(s_pio, s_sm, s_offset, &c);
	pio_sm_set_enabled(s_pio, s_sm, true);

	s_pin    = pin;
	s_active = true;
	return true;
}

void pioUartRxEnd(void) {
	if (!s_active) return;
	pio_sm_set_enabled(s_pio, s_sm, false);
	pio_sm_clear_fifos(s_pio, s_sm);
	pio_remove_program_and_unclaim_sm(&pio_uart_rx_program, s_pio, s_sm, s_offset);
	gpio_set_function(s_pin, GPIO_FUNC_NULL);
	gpio_disable_pulls(s_pin);
	s_pio    = nullptr;
	s_active = false;
}

bool pioUartRxActive(void) { return s_active; }

bool pioUartRxReadable(void) {
	return s_active && !pio_sm_is_rx_fifo_empty(s_pio, s_sm);
}

uint8_t pioUartRxGetc(void) {
	if (!pioUartRxReadable()) return 0;
	return (uint8_t)(pio_sm_get(s_pio, s_sm) >> 24);
}
