/**
 * @file pio_uart_rx.h
 * @brief A receive-only UART on a PIO state machine, so the telemetry wire can
 *        land on any GPIO.
 *
 * The RP2350's two hardware UARTs can only receive on GPIOs where `n % 4 == 1`
 * — eight pins out of thirty, and which of the two instances each belongs to
 * alternates in pairs of groups. That was fine while the KISS pin was a
 * compile-time constant on a board with pins to spare, and useless on the 2.8":
 * its free-GPIO mask is GP28 and GP29, only GP29 can receive, and GP29 is where
 * the ESC signal wire goes. Enabling KISS there found no pin at all and
 * validation switched it straight back off, which is exactly as helpful as it
 * sounds.
 *
 * A state machine samples whichever pin it is told to. So the rule "the KISS
 * pin must be a UART RX pin" is gone, along with the function that enforced it,
 * and any free GPIO that is not the ESC pin will do.
 *
 * @section pio_uart_rx_cost What it costs
 *
 * One state machine and nine instruction slots, claimed from whichever PIO
 * block has room — never a fixed block, because bidirectional DShot is always
 * resident and the 2.8" also carries the SDIO card driver.
 *
 * The FIFO is the one real difference from the hardware UART: eight bytes with
 * the TX half joined on, against the PL011's thirty-two. At 115200 baud that is
 * about 700 us of slack, so the receiver is drained on every pass of core1's
 * loop rather than once per DShot frame. @see escTaskPoll()
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

/**
 * @brief Claim a state machine and start receiving on @p pin.
 *
 * Releases any receiver already running first, so this doubles as "move to a
 * different pin". Receive only: the ESC talks and we never answer, and driving
 * a line the ESC is already driving is not a thing to do by accident.
 *
 * @param pin  GPIO to receive on. Any of them.
 * @param baud Bits per second. 8N1 is assumed.
 * @return true if a state machine was free and the receiver is running.
 */
bool pioUartRxBegin(uint8_t pin, uint32_t baud);

/**
 * @brief Stop receiving, unclaim the state machine, and let the pin float.
 *
 * Safe to call when nothing is running. The pin is handed back as a
 * high-impedance input: whatever owns it next — the ESC signal, after a pin
 * change on the SETUP screen — must not find it still driven by a PIO.
 */
void pioUartRxEnd(void);

/**
 * @brief Whether a receiver is currently running.
 * @return true between a successful pioUartRxBegin() and pioUartRxEnd().
 */
bool pioUartRxActive(void);

/**
 * @brief Whether at least one byte is waiting.
 * @return true if pioUartRxGetc() would return a received byte.
 */
bool pioUartRxReadable(void);

/**
 * @brief Take one byte from the receiver.
 *
 * Never blocks. Check pioUartRxReadable() first; an empty receiver reads 0,
 * which is also a perfectly good data byte and so is not a status.
 *
 * @return The byte, or 0 if nothing was waiting.
 */
uint8_t pioUartRxGetc(void);

#ifdef __cplusplus
}
#endif
