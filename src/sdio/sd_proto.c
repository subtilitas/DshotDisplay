/*
 * SPDX-License-Identifier: MIT
 *
 * Written from the SD Physical Layer Simplified Specification v9.10 and the
 * RP2350 datasheet; no third-party SD host-driver source was consulted.
 */

/**
 * @file sd_proto.c
 * @brief SD-mode wire formats. See sd_proto.h.
 */

#include "sd_proto.h"

#include <string.h>

uint8_t sdp_crc7(const uint8_t *data, size_t len) {
	/* G(x) = x^7 + x^3 + 1: feedback into bits 3 and 0. */
	uint8_t crc = 0;
	for (size_t i = 0; i < len; i++) {
		for (int b = 7; b >= 0; b--) {
			unsigned in = (data[i] >> b) & 1u;
			unsigned fb = ((crc >> 6) & 1u) ^ in;
			crc = (uint8_t)((crc << 1) & 0x7Fu);
			if (fb) crc ^= 0x09u;
		}
	}
	return crc;
}

uint16_t sdp_crc16(const uint8_t *data, size_t len) {
	/* G(x) = x^16 + x^12 + x^5 + 1. */
	uint16_t crc = 0;
	for (size_t i = 0; i < len; i++) {
		for (int b = 7; b >= 0; b--) {
			unsigned in = (data[i] >> b) & 1u;
			unsigned fb = ((crc >> 15) & 1u) ^ in;
			crc = (uint16_t)(crc << 1);
			if (fb) crc ^= 0x1021u;
		}
	}
	return crc;
}

/*
 * Four CRC16s one bit apart are one CRC over the nibble stream with the
 * polynomial G(x^4) = x^64 + x^48 + x^20 + 1. The derivation is in
 * docs/design/sdio-driver.md. Entry n is the remainder of the nibble n alone,
 * shifted through four steps; the host suite regenerates the table from the
 * polynomial.
 */
static const uint64_t kCrcX4[16] = {
	0x0000000000000000ull, 0x0001000000100001ull,
	0x0002000000200002ull, 0x0003000000300003ull,
	0x0004000000400004ull, 0x0005000000500005ull,
	0x0006000000600006ull, 0x0007000000700007ull,
	0x0008000000800008ull, 0x0009000000900009ull,
	0x000A000000A0000Aull, 0x000B000000B0000Bull,
	0x000C000000C0000Cull, 0x000D000000D0000Dull,
	0x000E000000E0000Eull, 0x000F000000F0000Full,
};

uint64_t sdp_crc16x4(const uint8_t *data, size_t len) {
	uint64_t crc = 0;
	for (size_t i = 0; i < len; i++) {
		crc = (crc << 4) ^ kCrcX4[(crc >> 60) ^ (data[i] >> 4)];
		crc = (crc << 4) ^ kCrcX4[(crc >> 60) ^ (data[i] & 0x0Fu)];
	}
	return crc;
}

uint16_t sdp_crc16x4_line(uint64_t crc, unsigned line) {
	/* Bit m of a line's CRC sits at bit 4m + line of the interleaved one. */
	uint16_t out = 0;
	for (unsigned m = 0; m < 16; m++)
		out |= (uint16_t)(((crc >> (4u * m + line)) & 1u) << m);
	return out;
}

uint64_t sdp_cmd_frame(uint8_t index, uint32_t arg) {
	uint8_t b[5] = {
		(uint8_t)(0x40u | (index & 0x3Fu)),
		(uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
		(uint8_t)(arg >> 8), (uint8_t)arg,
	};
	uint64_t f = 0;
	for (int i = 0; i < 5; i++) f = (f << 8) | b[i];
	return (f << 8) | ((uint64_t)sdp_crc7(b, 5) << 1) | 1u;
}

void sdp_cmd_words(uint8_t index, uint32_t arg, unsigned resp_bits, uint32_t w[2]) {
	uint64_t f = sdp_cmd_frame(index, arg);
	/* A command without a response still names a count: the program waits
	 * for a start bit either way, and the caller stops it. */
	unsigned rx = (resp_bits ? resp_bits : 48u) - 1u + SDP_TRAIL_CLOCKS;
	w[0] = (47u << 24) | (uint32_t)((f >> 24) & 0xFFFFFFu);
	w[1] = ((uint32_t)(f & 0xFFFFFFu) << 8) | (rx & 0xFFu);
}

unsigned sdp_resp_bits(sdp_resp_t kind) {
	switch (kind) {
		case SDP_RESP_NONE: return 0;
		case SDP_RESP_R2:   return 136;
		default:            return 48;
	}
}

unsigned sdp_resp_words(unsigned resp_bits) {
	return (resp_bits - 1u + SDP_TRAIL_CLOCKS + 31u) / 32u;
}

/** @brief Bit @p k of the captured stream, first captured bit is 0. */
static unsigned streamBit(const uint32_t *w, unsigned nbits, unsigned k) {
	unsigned full = nbits / 32u;
	if (k / 32u < full) return (w[k / 32u] >> (31u - k % 32u)) & 1u;
	unsigned rest = nbits % 32u;
	return (w[full] >> (rest - 1u - (k - full * 32u))) & 1u;
}

void sdp_resp_unpack(const uint32_t *w, unsigned resp_bits, uint8_t *out) {
	unsigned nbits = resp_bits - 1u + SDP_TRAIL_CLOCKS;
	memset(out, 0, resp_bits / 8u);
	/* Bit 0 of the response is the start bit, 0, and was not captured. */
	for (unsigned i = 1; i < resp_bits; i++)
		out[i / 8u] |= (uint8_t)(streamBit(w, nbits, i - 1u) << (7u - i % 8u));
}

sd_err_t sdp_resp48_check(const uint8_t r[6], sdp_resp_t kind, uint8_t index) {
	if ((r[0] & 0xC0u) != 0 || (r[5] & 1u) == 0) return SDE_FRAME;
	if (kind == SDP_RESP_R3)
		return ((r[0] & 0x3Fu) == 0x3Fu && (r[5] >> 1) == 0x7Fu) ? SDE_OK : SDE_FRAME;
	if ((r[0] & 0x3Fu) != (index & 0x3Fu)) return SDE_FRAME;
	return sdp_crc7(r, 5) == (r[5] >> 1) ? SDE_OK : SDE_CRC;
}

uint32_t sdp_resp48_arg(const uint8_t r[6]) {
	return ((uint32_t)r[1] << 24) | ((uint32_t)r[2] << 16) |
	       ((uint32_t)r[3] << 8) | r[4];
}

sd_err_t sdp_r2_check(const uint8_t r[17], uint8_t reg[16]) {
	if (r[0] != 0x3Fu || (r[16] & 1u) == 0) return SDE_FRAME;
	memcpy(reg, r + 1, 16);
	return sdp_crc7(reg, 15) == (reg[15] >> 1) ? SDE_OK : SDE_CRC;
}

sd_err_t sdp_status_error(uint32_t status, uint32_t ignore) {
	uint32_t e = status & SDP_ST_ERROR_MASK & ~ignore;
	if (!e) return SDE_OK;
	if (e & SDP_ST_WP_VIOLATION) return SDE_PROTECTED;
	if (e & (SDP_ST_OUT_OF_RANGE | SDP_ST_ADDRESS_ERROR | SDP_ST_BLOCK_LEN_ERROR))
		return SDE_RANGE;
	return SDE_STATUS;
}

unsigned sdp_status_state(uint32_t status) {
	return (status >> 9) & 0xFu;
}

uint32_t sdp_r6_status(uint32_t arg) {
	uint32_t s = arg & 0x1FFFu;
	if (arg & 0x8000u) s |= SDP_ST_COM_CRC_ERROR;
	if (arg & 0x4000u) s |= SDP_ST_ILLEGAL_COMMAND;
	if (arg & 0x2000u) s |= SDP_ST_ERROR;
	return s;
}

uint32_t sdp_reg_bits(const uint8_t reg[16], unsigned msb, unsigned lsb) {
	uint32_t v = 0;
	for (unsigned k = msb + 1u; k-- > lsb;)
		v = (v << 1) | ((reg[15u - k / 8u] >> (k % 8u)) & 1u);
	return v;
}

uint32_t sdp_csd_sectors(const uint8_t csd[16]) {
	switch (sdp_reg_bits(csd, 127, 126)) {
		case 0: {
			/* 5.3.2: (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) blocks of
			 * 2^READ_BL_LEN bytes. 2 GB cards use 1024-byte blocks. */
			uint32_t c_size = sdp_reg_bits(csd, 73, 62);
			uint32_t mult   = sdp_reg_bits(csd, 49, 47);
			uint32_t bl_len = sdp_reg_bits(csd, 83, 80);
			if (bl_len < 9 || bl_len > 11) return 0;
			return (c_size + 1u) << (mult + 2u + bl_len - 9u);
		}
		case 1: {
			/* 5.3.3: (C_SIZE + 1) * 512 KiB. The largest C_SIZE is one
			 * sector past 32 bits; the last sector is not addressable. */
			uint64_t n = ((uint64_t)sdp_reg_bits(csd, 69, 48) + 1u) * 1024u;
			return n > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)n;
		}
		default:
			return 0;
	}
}

uint32_t sdp_csd_max_hz(const uint8_t csd[16]) {
	/* Table 5-6. Values times ten, units divided by ten. */
	static const uint8_t kValue10[16] = {
		0, 10, 12, 13, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70, 80,
	};
	static const uint32_t kUnit10[4] = { 10000u, 100000u, 1000000u, 10000000u };
	uint32_t ts = sdp_reg_bits(csd, 103, 96);
	unsigned unit = ts & 7u, value = (ts >> 3) & 0xFu;
	if (unit > 3 || value == 0) return 0;
	return kValue10[value] * kUnit10[unit];
}

void sdp_tx_pack(const uint8_t *data, uint32_t out[SDP_TX_WORDS]) {
	out[0] = (SDP_TX_NIBBLES_M1 << 16) | 0xFFF0u;
	for (unsigned i = 0; i < SDP_BLOCK_BYTES / 4u; i++) {
		const uint8_t *p = data + 4u * i;
		out[1u + i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
		              ((uint32_t)p[2] << 8) | p[3];
	}
	uint64_t crc = sdp_crc16x4(data, SDP_BLOCK_BYTES);
	out[129] = (uint32_t)(crc >> 32);
	out[130] = (uint32_t)crc;
	out[131] = 0xFFFFFFFFu;
}

bool sdp_rx_check(const uint8_t *data, size_t len, const uint32_t crc[2]) {
	uint64_t c = sdp_crc16x4(data, len);
	return crc[0] == (uint32_t)(c >> 32) && crc[1] == (uint32_t)c;
}

int sdp_crc_status(const uint32_t w[2]) {
	unsigned bits = 0;   /* DAT0 of nibble k at bit 15 - k */
	for (unsigned k = 0; k < 16; k++)
		bits = (bits << 1) | ((w[k / 8u] >> (28u - 4u * (k % 8u))) & 1u);
	for (unsigned k = 0; k + 4u < 16u; k++) {
		if ((bits >> (15u - k)) & 1u) continue;
		if (((bits >> (15u - k - 4u)) & 1u) == 0) return -1;   /* no end bit */
		return (int)((bits >> (15u - k - 3u)) & 7u);
	}
	return -1;
}
