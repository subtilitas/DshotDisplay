/*
 * SPDX-License-Identifier: MIT
 *
 * Written from the SD Physical Layer Simplified Specification v9.10 and the
 * RP2350 datasheet; no third-party SD host-driver source was consulted.
 */

/**
 * @file sd_host.c
 * @brief The SD-mode card protocol. See sd_host.h.
 *
 * One card, one caller at a time: the glue holds the card's lock around every
 * entry point, which is what makes the static buffers here safe.
 */

#include "sd_host.h"

#include <stddef.h>
#include <string.h>

/** @brief CRC words of the blocks of one read command, FIFO order. */
static uint32_t s_crc[2u * SDH_MAX_BLOCKS];
/** @brief Aligned block for callers whose buffer is not. */
static uint32_t s_bounce[SDP_BLOCK_BYTES / 4u];
/** @brief Transmit stream of the block being written. */
static uint32_t s_tx[SDP_TX_WORDS];

/** @brief Wait for DAT0 high. */
static sd_err_t busyWait(const sdh_bus_t *b, uint32_t timeout_us) {
	uint32_t t0 = b->now_us(b->ctx);
	while (!b->dat0(b->ctx)) {
		if ((uint32_t)(b->now_us(b->ctx) - t0) >= timeout_us) return SDE_TIMEOUT;
		b->sleep_us(b->ctx, 2);
	}
	return SDE_OK;
}

/**
 * @brief One command and its response, checked.
 *
 * The 32-bit field of a 48-bit response goes to arg_out and the register of
 * an R2 to reg; either may be NULL when the caller does not want it.
 */
static sd_err_t cmd(const sdh_bus_t *b, uint8_t index, uint32_t arg, sdp_resp_t kind,
                    uint32_t *arg_out, uint8_t *reg) {
	uint8_t r[17];
	sd_err_t e = b->command(b->ctx, index, arg, sdp_resp_bits(kind), r);
	if (e != SDE_OK || kind == SDP_RESP_NONE) return e;
	if (kind == SDP_RESP_R2) return sdp_r2_check(r, reg);
	e = sdp_resp48_check(r, kind, index);
	if (e != SDE_OK) return e;
	if (arg_out) *arg_out = sdp_resp48_arg(r);
	return kind == SDP_RESP_R1B ? busyWait(b, SDH_BUSY_TIMEOUT_US) : SDE_OK;
}

/** @brief An R1 or R1b command whose card status must be clean. */
static sd_err_t cmdR1(const sdh_bus_t *b, uint8_t index, uint32_t arg, sdp_resp_t kind,
                      uint32_t ignore) {
	uint32_t st = 0;
	sd_err_t e = cmd(b, index, arg, kind, &st, NULL);
	return e != SDE_OK ? e : sdp_status_error(st, ignore);
}

/** @brief CMD55 then an application command with an R1. */
static sd_err_t acmdR1(const sdh_bus_t *b, const sdh_card_t *c, uint8_t index, uint32_t arg) {
	sd_err_t e = cmdR1(b, 55, (uint32_t)c->rca << 16, SDP_RESP_R1, 0);
	return e != SDE_OK ? e : cmdR1(b, index, arg, SDP_RESP_R1, 0);
}

/** @brief Command argument for a sector: block number or byte address, 4.3.14. */
static uint32_t address(const sdh_card_t *c, uint32_t lba) {
	return c->block_addr ? lba : lba * SDP_BLOCK_BYTES;
}

/** @brief Worth another attempt: the bus, not the card's verdict, failed. */
static bool retryable(sd_err_t e) {
	return e == SDE_CRC || e == SDE_FRAME || e == SDE_TIMEOUT || e == SDE_NO_RESPONSE;
}

bool sdh_err_is_fatal(sd_err_t e) {
	return e == SDE_NO_RESPONSE || e == SDE_TIMEOUT;
}

/** @brief Stop the clock and pass an error through. */
static sd_err_t finish(const sdh_bus_t *b, sd_err_t e) {
	b->stop_clock(b->ctx);
	return e;
}

sd_err_t sdh_init(const sdh_bus_t *b, sdh_card_t *c, uint32_t data_hz) {
	memset(c, 0, sizeof *c);
	b->set_clock(b->ctx, SDH_INIT_HZ);
	b->run_clock(b->ctx, SDH_POWERUP_US);

	sd_err_t e = cmd(b, 0, 0, SDP_RESP_NONE, NULL, NULL);
	if (e != SDE_OK) return finish(b, e);

	/* CMD8: no response is a version 1 card, a voltage it refuses, or no
	 * card. A response that does not echo is a card not to use. */
	uint32_t r7 = 0;
	e = cmd(b, 8, SDP_CMD8_ARG, SDP_RESP_R7, &r7, NULL);
	if (e == SDE_OK) {
		if ((r7 & 0xFFFu) != SDP_CMD8_ARG) return finish(b, SDE_UNUSABLE);
		c->v2 = true;
	} else if (e != SDE_NO_RESPONSE) {
		return finish(b, e);
	}

	/* ACMD41 until power-up is done, for more than 1 s from the first. */
	uint32_t arg = SDP_OCR_WINDOW | (c->v2 ? SDP_OCR_CCS : 0u);
	uint32_t t0 = b->now_us(b->ctx);
	for (;;) {
		e = cmd(b, 55, 0, SDP_RESP_R1, NULL, NULL);
		if (e != SDE_OK) return finish(b, e);
		e = cmd(b, 41, arg, SDP_RESP_R3, &c->ocr, NULL);
		/* A card outside the window goes inactive and stops answering. */
		if (e == SDE_NO_RESPONSE) return finish(b, SDE_VOLTAGE);
		if (e != SDE_OK) return finish(b, e);
		if ((c->ocr & SDP_OCR_WINDOW) == 0) return finish(b, SDE_VOLTAGE);
		if (c->ocr & SDP_OCR_BUSY) break;
		if ((uint32_t)(b->now_us(b->ctx) - t0) >= SDH_ACMD41_TIMEOUT_US)
			return finish(b, SDE_TIMEOUT);
		b->stop_clock(b->ctx);
		b->sleep_us(b->ctx, SDH_ACMD41_POLL_US);
	}
	c->block_addr = c->v2 && (c->ocr & SDP_OCR_CCS);

	e = cmd(b, 2, 0, SDP_RESP_R2, NULL, c->cid);
	if (e != SDE_OK) return finish(b, e);

	/* RCA 0 is reserved for deselection; ask again for another. */
	uint32_t r6 = 0;
	for (int i = 0; i < 3 && c->rca == 0; i++) {
		e = cmd(b, 3, 0, SDP_RESP_R6, &r6, NULL);
		if (e != SDE_OK) return finish(b, e);
		e = sdp_status_error(sdp_r6_status(r6), 0);
		if (e != SDE_OK) return finish(b, e);
		c->rca = (uint16_t)(r6 >> 16);
	}
	if (c->rca == 0) return finish(b, SDE_UNUSABLE);

	e = cmd(b, 9, (uint32_t)c->rca << 16, SDP_RESP_R2, NULL, c->csd);
	if (e != SDE_OK) return finish(b, e);
	c->sectors = sdp_csd_sectors(c->csd);
	if (c->sectors == 0) return finish(b, SDE_UNUSABLE);

	e = cmdR1(b, 7, (uint32_t)c->rca << 16, SDP_RESP_R1B, 0);
	if (e == SDE_OK) e = acmdR1(b, c, 6, 2);   /* four-bit bus */
	if (e == SDE_OK && !c->block_addr) e = cmdR1(b, 16, SDP_BLOCK_BYTES, SDP_RESP_R1, 0);
	if (e != SDE_OK) return finish(b, e);

	uint32_t hz = sdp_csd_max_hz(c->csd);
	if (hz == 0 || hz > SDH_MAX_HZ) hz = SDH_MAX_HZ;
	if (data_hz < hz) hz = data_hz;
	c->clock_hz = b->set_clock(b->ctx, hz);

	uint32_t st = 0;
	e = sdh_status(b, c, &st);
	if (e == SDE_OK && sdp_status_state(st) != SDP_STATE_TRAN) e = SDE_STATUS;
	if (e != SDE_OK) return finish(b, e);
	c->ready = true;
	return finish(b, SDE_OK);
}

sd_err_t sdh_status(const sdh_bus_t *b, sdh_card_t *c, uint32_t *status) {
	return cmd(b, 13, (uint32_t)c->rca << 16, SDP_RESP_R1, status, NULL);
}

/** @brief One read command of @p n blocks into an aligned buffer. */
static sd_err_t readChunk(const sdh_bus_t *b, const sdh_card_t *c, uint32_t lba,
                          uint32_t n, uint8_t *buf) {
	sd_err_t e = b->rx_arm(b->ctx, buf, SDP_BLOCK_BYTES, n, s_crc);
	if (e != SDE_OK) return e;
	e = cmdR1(b, n == 1 ? 17 : 18, address(c, lba), SDP_RESP_R1, 0);
	bool accepted = (e == SDE_OK);
	if (accepted) e = b->rx_wait(b->ctx, SDH_READ_TIMEOUT_US);
	b->rx_stop(b->ctx);
	/* CMD12 ends a multi-block read, and pulls a card that stalled in the
	 * middle of any read back to the transfer state. OUT_OF_RANGE after the
	 * last block of the card is expected, 4.3.3. */
	if (accepted && (n > 1 || e != SDE_OK)) {
		sd_err_t e2 = cmdR1(b, 12, 0, SDP_RESP_R1B, SDP_ST_OUT_OF_RANGE);
		if (e == SDE_OK) e = e2;
	}
	b->stop_clock(b->ctx);
	if (e != SDE_OK) return e;
	for (uint32_t i = 0; i < n; i++)
		if (!sdp_rx_check(buf + i * SDP_BLOCK_BYTES, SDP_BLOCK_BYTES, &s_crc[2u * i]))
			return SDE_CRC;
	return SDE_OK;
}

/** @brief Transmit-stream time for one block plus margin, us. */
static uint32_t txTimeoutUs(uint32_t hz) {
	return 2u * (uint32_t)((1100ull * 1000000u) / hz) + 1000u;
}

/** @brief One write command of @p n blocks, through programming. */
static sd_err_t writeChunk(const sdh_bus_t *b, const sdh_card_t *c, uint32_t lba,
                           uint32_t n, const uint8_t *buf) {
	sd_err_t e = cmdR1(b, n == 1 ? 24 : 25, address(c, lba), SDP_RESP_R1, 0);
	if (e != SDE_OK) return finish(b, e);
	for (uint32_t i = 0; i < n && e == SDE_OK; i++) {
		uint32_t tok[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
		sdp_tx_pack(buf + i * SDP_BLOCK_BYTES, s_tx);
		e = b->tx_block(b->ctx, s_tx, SDP_TX_WORDS, tok, txTimeoutUs(c->clock_hz));
		if (e == SDE_OK) {
			int t = sdp_crc_status(tok);
			e = t == 2 ? SDE_OK : t == 5 ? SDE_CRC : t < 0 ? SDE_FRAME : SDE_WRITE;
		}
		/* Busy follows the token whatever it said. */
		sd_err_t eb = busyWait(b, SDH_BUSY_TIMEOUT_US);
		if (e == SDE_OK) e = eb;
	}
	if (n > 1) {
		sd_err_t e2 = cmdR1(b, 12, 0, SDP_RESP_R1B, 0);
		if (e == SDE_OK) e = e2;
	}
	/* Errors found while programming show in the next status, 4.10.1. */
	if (e == SDE_OK) e = cmdR1(b, 13, (uint32_t)c->rca << 16, SDP_RESP_R1, 0);
	return finish(b, e);
}

/** @brief Common argument checks of read and write. */
static sd_err_t checkRange(const sdh_card_t *c, uint32_t lba, uint32_t count, const void *buf) {
	if (!c->ready) return SDE_NOT_READY;
	if (!buf || count == 0 || lba >= c->sectors || count > c->sectors - lba) return SDE_PARAM;
	return SDE_OK;
}

sd_err_t sdh_read(const sdh_bus_t *b, sdh_card_t *c, uint32_t lba, uint32_t count, uint8_t *buf) {
	sd_err_t e = checkRange(c, lba, count, buf);
	while (e == SDE_OK && count) {
		bool aligned = ((uintptr_t)buf & 3u) == 0;
		uint32_t n = !aligned ? 1u : count < SDH_MAX_BLOCKS ? count : SDH_MAX_BLOCKS;
		uint8_t *dst = aligned ? buf : (uint8_t *)s_bounce;
		for (unsigned a = 0; a < SDH_ATTEMPTS; a++) {
			e = readChunk(b, c, lba, n, dst);
			if (!retryable(e)) break;
		}
		if (e != SDE_OK) break;
		if (!aligned) memcpy(buf, s_bounce, SDP_BLOCK_BYTES);
		buf += n * SDP_BLOCK_BYTES;
		lba += n;
		count -= n;
	}
	if (sdh_err_is_fatal(e)) c->ready = false;
	return e;
}

sd_err_t sdh_write(const sdh_bus_t *b, sdh_card_t *c, uint32_t lba, uint32_t count,
                   const uint8_t *buf) {
	sd_err_t e = checkRange(c, lba, count, buf);
	while (e == SDE_OK && count) {
		uint32_t n = count < SDH_MAX_BLOCKS ? count : SDH_MAX_BLOCKS;
		for (unsigned a = 0; a < SDH_ATTEMPTS; a++) {
			e = writeChunk(b, c, lba, n, buf);
			if (!retryable(e)) break;
		}
		buf += n * SDP_BLOCK_BYTES;
		lba += n;
		count -= n;
	}
	if (sdh_err_is_fatal(e)) c->ready = false;
	return e;
}

sd_err_t sdh_sd_status(const sdh_bus_t *b, sdh_card_t *c, uint8_t out[64]) {
	if (!c->ready) return SDE_NOT_READY;
	sd_err_t e = b->rx_arm(b->ctx, (uint8_t *)s_bounce, 64, 1, s_crc);
	if (e == SDE_OK) e = acmdR1(b, c, 13, 0);
	if (e == SDE_OK) e = b->rx_wait(b->ctx, SDH_READ_TIMEOUT_US);
	b->rx_stop(b->ctx);
	b->stop_clock(b->ctx);
	if (e == SDE_OK && !sdp_rx_check((const uint8_t *)s_bounce, 64, s_crc)) e = SDE_CRC;
	if (e == SDE_OK) memcpy(out, s_bounce, 64);
	if (sdh_err_is_fatal(e)) c->ready = false;
	return e;
}

bool sdh_probe(const sdh_bus_t *b, sdh_card_t *c) {
	if (c->ready) {
		uint32_t st = 0;
		sd_err_t e = sdh_status(b, c, &st);
		b->stop_clock(b->ctx);
		if (sdh_err_is_fatal(e)) c->ready = false;
		return e == SDE_OK;
	}
	b->set_clock(b->ctx, SDH_INIT_HZ);
	b->run_clock(b->ctx, SDH_POWERUP_US);
	cmd(b, 0, 0, SDP_RESP_NONE, NULL, NULL);
	uint32_t r7 = 0;
	bool seen = cmd(b, 8, SDP_CMD8_ARG, SDP_RESP_R7, &r7, NULL) != SDE_NO_RESPONSE ||
	            cmd(b, 55, 0, SDP_RESP_R1, NULL, NULL) != SDE_NO_RESPONSE;
	b->stop_clock(b->ctx);
	return seen;
}
