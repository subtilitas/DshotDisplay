/*
 * SPDX-License-Identifier: MIT
 *
 * Written from the SD Physical Layer Simplified Specification v9.10 and the
 * RP2350 datasheet; no third-party SD host-driver source was consulted.
 */

/**
 * @file sd_pio.h
 * @brief The four-bit SD bus on RP2350 PIO and DMA, as an sdh_bus_t.
 *
 * Thin by design: it moves words between the programs in sd_pio.pio and
 * memory, and waits with a timeout. Everything the protocol decides is in
 * sd_host.c. PIO is programmable input/output, DMA direct memory access.
 * See docs/design/sdio-driver.md.
 *
 * Resources once claimed: two state machines and all 32 instruction slots of
 * one PIO block, and two DMA channels. No interrupt. One bus at a time.
 */

#ifndef SD_PIO_H
#define SD_PIO_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"

#include "sd_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One bus: pins, the resources it holds, and receive progress. */
typedef struct {
	PIO      pio;          /**< The block both programs are in. */
	uint8_t  clk;          /**< CLK GPIO. */
	uint8_t  cmd;          /**< CMD GPIO. */
	uint8_t  d0;           /**< DAT0 GPIO; DAT1..DAT3 follow it. */
	uint8_t  sm_cmd;       /**< State machine running sdpio_cmd. */
	uint8_t  sm_dat;       /**< State machine running sdpio_dat. */
	uint8_t  off_cmd;      /**< Load offset of sdpio_cmd. */
	uint8_t  off_dat;      /**< Load offset of sdpio_dat. */
	uint8_t  dma_data;     /**< Channel that moves data. */
	uint8_t  dma_ctrl;     /**< Channel that reprograms dma_data from a table. */
	bool     claimed;      /**< Resources held. */
	bool     running;      /**< The clock is toggling. */
	uint32_t clk_hz;       /**< Clock rate set, Hz. */
	uint32_t y;            /**< Nibbles per block less one, in the data machine's Y. */
	uint32_t rx_words;     /**< CRC words of the armed read. */
	uint32_t *rx_crc;      /**< Where they land. */
	const uint32_t *rx_end; /**< End of the armed control-block table. */
} sdpio_t;

/**
 * @brief Claim the PIO and DMA resources and set up the pins.
 *
 * CLK and CMD can be any GPIO below 32; DAT0..DAT3 are @p d0 to @p d0 + 3.
 * Internal pull-ups go on CMD and DAT; no pull-down is ever enabled
 * (RP2350-E9). Nothing is claimed when anything is missing.
 *
 * @param h   The bus, zeroed or released.
 * @param pio The block, which must have all 32 instruction slots free.
 * @param clk CLK GPIO.
 * @param cmd CMD GPIO.
 * @param d0  DAT0 GPIO.
 * @return SDE_OK, SDE_PARAM for pins out of range, or SDE_HW.
 */
sd_err_t sdpio_claim(sdpio_t *h, PIO pio, unsigned clk, unsigned cmd, unsigned d0);

/**
 * @brief Stop both machines and give every resource back.
 * @param h The bus.
 */
void sdpio_release(sdpio_t *h);

/**
 * @brief Fill a bus table whose functions drive @p h.
 * @param h   A claimed bus.
 * @param bus The table.
 */
void sdpio_bus(sdpio_t *h, sdh_bus_t *bus);

#ifdef __cplusplus
}
#endif

#endif /* SD_PIO_H */
