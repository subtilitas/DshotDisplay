/*
 * SPDX-License-Identifier: MIT
 *
 * Written from the SD Physical Layer Simplified Specification v9.10 and the
 * RP2350 datasheet; no third-party SD host-driver source was consulted.
 */

/**
 * @file sd_pio.c
 * @brief The four-bit SD bus on RP2350 PIO and DMA. See sd_pio.h.
 *
 * Section numbers in comments refer to the RP2350 datasheet.
 */

#include "sd_pio.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

#include "sd_pio.pio.h"

/** @brief Words per control block: CTRL, WRITE_ADDR, TRANS_COUNT, READ_ADDR_TRIG. */
#define CTRL_WORDS    4u
/** @brief Bound on a DMA abort, us (12.6.8.3). */
#define DMA_ABORT_US  100u

/**
 * @brief Control blocks of the armed read: data, then CRC, per block.
 *
 * Aligned to 16 bytes like the alias-3 registers it is copied into, though
 * only the write side wraps.
 */
static uint32_t s_table[2u * SDH_MAX_BLOCKS * CTRL_WORDS] __attribute__((aligned(16)));
/** @brief The bus holding the resources, or NULL. */
static sdpio_t *s_owner;

/** @brief Microseconds that @p n clocks take, rounded up. */
static uint32_t clocksUs(const sdpio_t *h, uint32_t n) {
	return (uint32_t)(((uint64_t)n * 1000000u + h->clk_hz - 1u) / h->clk_hz);
}

/** @brief Stop the command machine at `start`, CLK low, FIFOs empty. */
static void cmdHalt(sdpio_t *h) {
	pio_sm_set_enabled(h->pio, h->sm_cmd, false);
	pio_sm_restart(h->pio, h->sm_cmd);
	pio_sm_clear_fifos(h->pio, h->sm_cmd);
	pio_sm_exec(h->pio, h->sm_cmd,
	            pio_encode_jmp(h->off_cmd + sdpio_cmd_offset_start) | pio_encode_sideset(1, 0));
	h->running = false;
}

/** @brief Stop the data machine with DAT0..DAT3 released and FIFOs empty. */
static void datHalt(sdpio_t *h) {
	pio_sm_set_enabled(h->pio, h->sm_dat, false);
	pio_sm_exec(h->pio, h->sm_dat, pio_encode_set(pio_pindirs, 0));
	pio_sm_restart(h->pio, h->sm_dat);
	pio_sm_clear_fifos(h->pio, h->sm_dat);
}

/** @brief Load Y of the halted data machine through its OSR (11.4.10). */
static void datSetY(sdpio_t *h, uint32_t y) {
	pio_sm_put(h->pio, h->sm_dat, y);
	pio_sm_exec(h->pio, h->sm_dat, pio_encode_pull(false, true));
	pio_sm_exec(h->pio, h->sm_dat, pio_encode_mov(pio_y, pio_osr));
	pio_sm_restart(h->pio, h->sm_dat);   /* OSR counts as empty again, 11.2.4.2 */
	h->y = y;
}

/** @brief Abort both channels in the order 12.6.8.3 gives. */
static void dmaStop(sdpio_t *h) {
	const uint8_t ch[2] = { h->dma_data, h->dma_ctrl };
	uint32_t mask = (1u << ch[0]) | (1u << ch[1]);
	for (int i = 0; i < 2; i++) {
		hw_clear_bits(&dma_hw->ch[ch[i]].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
		hw_write_masked(&dma_hw->ch[ch[i]].al1_ctrl,
		                (uint32_t)ch[i] << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB,
		                DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS);
	}
	dma_hw->abort = mask;
	uint32_t t0 = time_us_32();
	while ((dma_hw->abort & mask) || dma_channel_is_busy(ch[0]) || dma_channel_is_busy(ch[1]))
		if (time_us_32() - t0 >= DMA_ABORT_US) break;
}

/** @brief Is the command program in its wait-for-start-bit loop? */
static bool cmdInWait(const sdpio_t *h) {
	uint pc = pio_sm_get_pc(h->pio, h->sm_cmd);
	uint at = h->off_cmd + sdpio_cmd_offset_wait_start;
	return pc == at || pc == at + 1u;
}

static sd_err_t busCommand(void *ctx, uint8_t index, uint32_t arg, unsigned resp_bits,
                           uint8_t *resp) {
	sdpio_t *h = (sdpio_t *)ctx;
	uint32_t w[2];
	sdp_cmd_words(index, arg, resp_bits, w);
	cmdHalt(h);
	pio_sm_put(h->pio, h->sm_cmd, w[0]);
	pio_sm_put(h->pio, h->sm_cmd, w[1]);
	pio_sm_set_enabled(h->pio, h->sm_cmd, true);
	h->running = true;
	uint32_t t0 = time_us_32();

	if (resp_bits == 0) {
		/* Sent once the program waits for a start bit that will not come;
		 * then the 8 clocks of 4.4, then stop. */
		while (!cmdInWait(h)) {
			if (time_us_32() - t0 >= SDH_CMD_TIMEOUT_US) {
				cmdHalt(h);
				return SDE_NO_RESPONSE;
			}
		}
		busy_wait_us_32(clocksUs(h, SDP_TRAIL_CLOCKS));
		cmdHalt(h);
		return SDE_OK;
	}

	uint32_t raw[5];
	unsigned n = sdp_resp_words(resp_bits);
	for (unsigned i = 0; i < n; i++) {
		while (pio_sm_is_rx_fifo_empty(h->pio, h->sm_cmd)) {
			if (time_us_32() - t0 >= SDH_CMD_TIMEOUT_US) {
				cmdHalt(h);
				return SDE_NO_RESPONSE;
			}
		}
		raw[i] = pio_sm_get(h->pio, h->sm_cmd);
	}
	sdp_resp_unpack(raw, resp_bits, resp);
	return SDE_OK;   /* the program now runs the free clock */
}

static void busStopClock(void *ctx);

static uint32_t busSetClock(void *ctx, uint32_t hz) {
	sdpio_t *h = (sdpio_t *)ctx;
	if (h->running) busStopClock(h);   /* a new rate starts from a stopped clock */
	uint64_t sys = clock_get_hz(clk_sys);
	if (hz == 0) hz = 1;
	/* Two instructions per clock; divider in 1/256 steps, rounded up so the
	 * rate never exceeds the request (11.5.5). At least 3 keeps both edge
	 * waits of the data program inside one clock period. */
	uint64_t div256 = (sys * 256u + 2ull * hz - 1u) / (2ull * hz);
	if (div256 < 3u * 256u) div256 = 3u * 256u;
	if (div256 > 0xFFFFFFull) div256 = 0xFFFFFFull;
	pio_sm_set_clkdiv_int_frac8(h->pio, h->sm_cmd, (uint32_t)(div256 >> 8), (uint8_t)div256);
	h->clk_hz = (uint32_t)((sys * 256u) / (2u * div256));
	return h->clk_hz;
}

static void busRunClock(void *ctx, uint32_t us) {
	sdpio_t *h = (sdpio_t *)ctx;
	cmdHalt(h);
	pio_sm_exec(h->pio, h->sm_cmd,
	            pio_encode_jmp(h->off_cmd + sdpio_cmd_offset_idle) | pio_encode_sideset(1, 0));
	pio_sm_set_enabled(h->pio, h->sm_cmd, true);
	h->running = true;
	busy_wait_us_32(us);
	cmdHalt(h);
}

static void busStopClock(void *ctx) {
	sdpio_t *h = (sdpio_t *)ctx;
	if (h->running) busy_wait_us_32(clocksUs(h, SDP_TRAIL_CLOCKS));
	cmdHalt(h);
}

static sd_err_t busRxArm(void *ctx, uint8_t *buf, uint32_t bytes, uint32_t blocks,
                         uint32_t *crc) {
	sdpio_t *h = (sdpio_t *)ctx;
	if (blocks == 0 || blocks > SDH_MAX_BLOCKS || bytes == 0 || (bytes & 3u) ||
	    ((uintptr_t)buf & 3u))
		return SDE_PARAM;
	dmaStop(h);
	datHalt(h);
	uint32_t y = 2u * bytes + 16u - 1u;
	if (y != h->y) datSetY(h, y);
	pio_sm_exec(h->pio, h->sm_dat, pio_encode_jmp(h->off_dat + sdpio_dat_offset_rx_start));

	/* Data channel: RX FIFO to memory, bytes swapped so the word whose top
	 * nibble arrived first lands as bytes in address order (12.6.2.3). */
	uint32_t rxf = (uint32_t)(uintptr_t)&h->pio->rxf[h->sm_dat];
	dma_channel_config d = dma_channel_get_default_config(h->dma_data);
	channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
	channel_config_set_read_increment(&d, false);
	channel_config_set_write_increment(&d, true);
	channel_config_set_dreq(&d, pio_get_dreq(h->pio, h->sm_dat, false));
	channel_config_set_bswap(&d, true);
	channel_config_set_high_priority(&d, true);
	channel_config_set_chain_to(&d, h->dma_ctrl);
	uint32_t more = channel_config_get_ctrl_value(&d);
	channel_config_set_chain_to(&d, h->dma_data);   /* to itself: the end */
	uint32_t last = channel_config_get_ctrl_value(&d);

	uint32_t *t = s_table;
	for (uint32_t i = 0; i < blocks; i++) {
		*t++ = more;
		*t++ = (uint32_t)(uintptr_t)(buf + i * bytes);
		*t++ = bytes / 4u;
		*t++ = rxf;
		*t++ = i + 1u < blocks ? more : last;
		*t++ = (uint32_t)(uintptr_t)(crc + 2u * i);
		*t++ = 2u;
		*t++ = rxf;
	}
	h->rx_end = t;
	h->rx_crc = crc;
	h->rx_words = 2u * blocks;

	/* Control channel: four words of the table into the data channel's
	 * alias 3, the last of which triggers it (12.6.3.1, 12.6.9.2). */
	dma_channel_config c = dma_channel_get_default_config(h->dma_ctrl);
	channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
	channel_config_set_read_increment(&c, true);
	channel_config_set_write_increment(&c, true);
	channel_config_set_ring(&c, true, 4);
	dma_channel_configure(h->dma_ctrl, &c, &dma_hw->ch[h->dma_data].al3_ctrl, s_table,
	                      CTRL_WORDS, false);
	pio_sm_set_enabled(h->pio, h->sm_dat, true);
	dma_channel_start(h->dma_ctrl);
	return SDE_OK;
}

static sd_err_t busRxWait(void *ctx, uint32_t timeout_us) {
	sdpio_t *h = (sdpio_t *)ctx;
	uint32_t t0 = time_us_32();
	uint32_t seen = dma_hw->ch[h->dma_data].write_addr;
	for (;;) {
		if (dma_hw->ch[h->dma_ctrl].read_addr == (uint32_t)(uintptr_t)h->rx_end &&
		    !dma_channel_is_busy(h->dma_ctrl) && !dma_channel_is_busy(h->dma_data))
			break;
		uint32_t now = time_us_32();
		uint32_t wa = dma_hw->ch[h->dma_data].write_addr;
		if (wa != seen) {
			seen = wa;
			t0 = now;
		} else if (now - t0 >= timeout_us) {
			dmaStop(h);
			return SDE_TIMEOUT;
		}
	}
	/* The CRC words went through the byte swap too; give them back in
	 * FIFO order. */
	for (uint32_t i = 0; i < h->rx_words; i++) h->rx_crc[i] = __builtin_bswap32(h->rx_crc[i]);
	return SDE_OK;
}

static void busRxStop(void *ctx) {
	sdpio_t *h = (sdpio_t *)ctx;
	dmaStop(h);
	datHalt(h);
}

static sd_err_t busTxBlock(void *ctx, const uint32_t *words, uint32_t nwords, uint32_t status[2],
                           uint32_t timeout_us) {
	sdpio_t *h = (sdpio_t *)ctx;
	dmaStop(h);
	datHalt(h);
	pio_sm_exec(h->pio, h->sm_dat, pio_encode_jmp(h->off_dat + sdpio_dat_offset_tx_start));
	dma_channel_config d = dma_channel_get_default_config(h->dma_data);
	channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
	channel_config_set_read_increment(&d, true);
	channel_config_set_write_increment(&d, false);
	channel_config_set_dreq(&d, pio_get_dreq(h->pio, h->sm_dat, true));
	channel_config_set_high_priority(&d, true);
	dma_channel_configure(h->dma_data, &d, &h->pio->txf[h->sm_dat], words, nwords, true);
	pio_sm_set_enabled(h->pio, h->sm_dat, true);

	uint32_t t0 = time_us_32();
	while (pio_sm_get_rx_fifo_level(h->pio, h->sm_dat) < 2u) {
		if (time_us_32() - t0 >= timeout_us) {
			dmaStop(h);
			datHalt(h);
			return SDE_TIMEOUT;
		}
	}
	status[0] = pio_sm_get(h->pio, h->sm_dat);
	status[1] = pio_sm_get(h->pio, h->sm_dat);
	datHalt(h);
	dmaStop(h);
	return SDE_OK;
}

static bool busDat0(void *ctx) {
	return gpio_get(((sdpio_t *)ctx)->d0);
}

static uint32_t busNow(void *ctx) {
	(void)ctx;
	return time_us_32();
}

static void busSleep(void *ctx, uint32_t us) {
	(void)ctx;
	busy_wait_us_32(us);
}

sd_err_t sdpio_claim(sdpio_t *h, PIO pio, unsigned clk, unsigned cmd, unsigned d0) {
	if (h->claimed) return SDE_OK;
	if (s_owner) return SDE_HW;
	if (clk >= 32u || cmd >= 32u || d0 + 3u >= 32u || clk == cmd ||
	    (clk >= d0 && clk <= d0 + 3u) || (cmd >= d0 && cmd <= d0 + 3u))
		return SDE_PARAM;

	int oc = -1, od = -1, sc = -1, sd = -1, dc = -1, dd = -1;
	if (pio_can_add_program(pio, &sdpio_cmd_program)) oc = pio_add_program(pio, &sdpio_cmd_program);
	if (oc >= 0 && pio_can_add_program(pio, &sdpio_dat_program))
		od = pio_add_program(pio, &sdpio_dat_program);
	if (od >= 0) sc = pio_claim_unused_sm(pio, false);
	if (sc >= 0) sd = pio_claim_unused_sm(pio, false);
	if (sd >= 0) dc = dma_claim_unused_channel(false);
	if (dc >= 0) dd = dma_claim_unused_channel(false);
	if (dd < 0) {
		if (dc >= 0) dma_channel_unclaim((uint)dc);
		if (sd >= 0) pio_sm_unclaim(pio, (uint)sd);
		if (sc >= 0) pio_sm_unclaim(pio, (uint)sc);
		if (od >= 0) pio_remove_program(pio, &sdpio_dat_program, (uint)od);
		if (oc >= 0) pio_remove_program(pio, &sdpio_cmd_program, (uint)oc);
		return SDE_HW;
	}

	memset(h, 0, sizeof *h);
	h->pio = pio;
	h->clk = (uint8_t)clk;
	h->cmd = (uint8_t)cmd;
	h->d0 = (uint8_t)d0;
	h->off_cmd = (uint8_t)oc;
	h->off_dat = (uint8_t)od;
	h->sm_cmd = (uint8_t)sc;
	h->sm_dat = (uint8_t)sd;
	h->dma_data = (uint8_t)dd;
	h->dma_ctrl = (uint8_t)dc;

	pio_gpio_init(pio, clk);
	pio_gpio_init(pio, cmd);
	gpio_disable_pulls(clk);
	gpio_pull_up(cmd);
	for (unsigned i = 0; i < 4u; i++) {
		pio_gpio_init(pio, d0 + i);
		gpio_pull_up(d0 + i);
	}

	pio_sm_config c = sdpio_cmd_program_get_default_config(h->off_cmd);
	sm_config_set_sideset_pins(&c, clk);
	sm_config_set_out_pins(&c, cmd, 1);
	sm_config_set_set_pins(&c, cmd, 1);
	sm_config_set_in_pins(&c, cmd);
	sm_config_set_jmp_pin(&c, cmd);
	sm_config_set_out_shift(&c, false, true, 32);
	sm_config_set_in_shift(&c, false, true, 32);
	pio_sm_init(pio, h->sm_cmd, h->off_cmd + sdpio_cmd_offset_start, &c);
	pio_sm_set_pins_with_mask(pio, h->sm_cmd, 1u << cmd, (1u << clk) | (1u << cmd));
	pio_sm_set_pindirs_with_mask(pio, h->sm_cmd, 1u << clk, (1u << clk) | (1u << cmd));

	pio_sm_config e = sdpio_dat_program_get_default_config(h->off_dat);
	sm_config_set_out_pins(&e, d0, 4);
	sm_config_set_set_pins(&e, d0, 4);
	sm_config_set_in_pins(&e, d0);
	sm_config_set_jmp_pin(&e, clk);
	sm_config_set_out_shift(&e, false, true, 32);
	sm_config_set_in_shift(&e, false, true, 32);
	pio_sm_init(pio, h->sm_dat, h->off_dat + sdpio_dat_offset_rx_start, &e);
	pio_sm_set_pins_with_mask(pio, h->sm_dat, 0xFu << d0, 0xFu << d0);
	pio_sm_set_pindirs_with_mask(pio, h->sm_dat, 0, 0xFu << d0);
	datSetY(h, 2u * SDP_BLOCK_BYTES + 16u - 1u);

	h->claimed = true;
	s_owner = h;
	busSetClock(h, SDH_INIT_HZ);
	cmdHalt(h);
	return SDE_OK;
}

void sdpio_release(sdpio_t *h) {
	if (!h->claimed) return;
	dmaStop(h);
	datHalt(h);
	cmdHalt(h);
	pio_sm_unclaim(h->pio, h->sm_cmd);
	pio_sm_unclaim(h->pio, h->sm_dat);
	pio_remove_program(h->pio, &sdpio_cmd_program, h->off_cmd);
	pio_remove_program(h->pio, &sdpio_dat_program, h->off_dat);
	dma_channel_unclaim(h->dma_data);
	dma_channel_unclaim(h->dma_ctrl);
	/* Back to plain inputs; the pull-ups stay, as the bus idles high. */
	gpio_init(h->clk);
	gpio_init(h->cmd);
	for (unsigned i = 0; i < 4u; i++) gpio_init(h->d0 + i);
	h->claimed = false;
	s_owner = NULL;
}

void sdpio_bus(sdpio_t *h, sdh_bus_t *bus) {
	bus->ctx = h;
	bus->command = busCommand;
	bus->set_clock = busSetClock;
	bus->run_clock = busRunClock;
	bus->stop_clock = busStopClock;
	bus->rx_arm = busRxArm;
	bus->rx_wait = busRxWait;
	bus->rx_stop = busRxStop;
	bus->tx_block = busTxBlock;
	bus->dat0 = busDat0;
	bus->now_us = busNow;
	bus->sleep_us = busSleep;
}
