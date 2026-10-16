/*
 * SPDX-License-Identifier: MIT
 *
 * Written from the SD Physical Layer Simplified Specification v9.10 and the
 * RP2350 datasheet; no third-party SD host-driver source was consulted.
 */

/**
 * @file test_sdio.cpp
 * @brief Host tests for the SDIO driver's protocol layers.
 *
 * Two halves. The first checks the wire formats in sd_proto.c against the
 * specification's own examples and against bitwise reference code written
 * here from the polynomials. The second runs sd_host.c against a simulated
 * card: a state machine that answers commands the way section 4 says a card
 * does, builds its responses into the FIFO (first-in, first-out queue) words
 * the PIO (programmable input/output) program would capture, delivers read
 * data the way the DMA (direct memory access) would, and decodes written
 * blocks line by line. The simulation has its own clock, so the 1 s ACMD41
 * limit and the busy timeouts run in no time.
 */

#include "check.h"

#include "../src/sdio/sd_proto.h"
#include "../src/sdio/sd_host.h"

#include <stdint.h>
#include <string.h>

#include <map>
#include <vector>

// ---------------------------------------------------------------------------
// Reference code, bit by bit from the polynomials in 4.5.
// ---------------------------------------------------------------------------

/** @brief CRC7 of the low @p nbits of @p v, most significant first, by long division. */
static unsigned refCrc7(uint64_t v, unsigned nbits) {
	uint64_t r = v << 7;
	for (int bit = (int)nbits + 6; bit >= 7; bit--)
		if ((r >> bit) & 1u) r ^= (uint64_t)0x89u << (bit - 7);   // x^7 + x^3 + 1
	return (unsigned)(r & 0x7Fu);
}

/** @brief CRC16 of a bit string, one bit per element. */
static uint16_t refCrc16Bits(const std::vector<uint8_t> &bits) {
	uint32_t r = 0;
	for (uint8_t b : bits) {
		r = (r << 1) | b;
		if (r & 0x10000u) r ^= 0x11021u;
	}
	for (int i = 0; i < 16; i++) {
		r <<= 1;
		if (r & 0x10000u) r ^= 0x11021u;
	}
	return (uint16_t)r;
}

/** @brief The bits one DAT line carries for a block, 3.6.1 Figure 3-9. */
static std::vector<uint8_t> laneBits(const uint8_t *data, size_t len, unsigned line) {
	std::vector<uint8_t> out;
	for (size_t i = 0; i < len; i++) {
		out.push_back((data[i] >> (4 + line)) & 1u);
		out.push_back((data[i] >> line) & 1u);
	}
	return out;
}

/** @brief The two CRC words of a block as the card sends them. */
static void refCrcWords(const uint8_t *data, size_t len, uint32_t w[2]) {
	uint16_t c[4];
	for (unsigned j = 0; j < 4; j++) c[j] = refCrc16Bits(laneBits(data, len, j));
	w[0] = w[1] = 0;
	for (int m = 15; m >= 0; m--) {
		unsigned nib = 0;
		for (unsigned j = 0; j < 4; j++) nib |= ((c[j] >> m) & 1u) << j;
		unsigned k = 15 - (unsigned)m;
		w[k / 8] |= (uint32_t)nib << (28 - 4 * (k % 8));
	}
}

/** @brief What the command program captures for a canonical response. */
static void packResp(const uint8_t *r, unsigned resp_bits, uint32_t *w) {
	unsigned nbits = resp_bits - 1 + SDP_TRAIL_CLOCKS;
	unsigned nw = (nbits + 31) / 32;
	for (unsigned i = 0; i < nw; i++) w[i] = 0;
	for (unsigned k = 0; k < nbits; k++) {
		unsigned i = k + 1;    // bit 0 of the response is the start bit
		unsigned bit = i < resp_bits ? (r[i / 8] >> (7 - i % 8)) & 1u : 1u;
		w[k / 32] = (w[k / 32] << 1) | bit;
	}
}

/** @brief Set bits @p msb..@p lsb of a 128-bit register. */
static void setBits(uint8_t reg[16], unsigned msb, unsigned lsb, uint32_t v) {
	for (unsigned k = lsb; k <= msb; k++) {
		uint8_t m = (uint8_t)(1u << (k % 8));
		uint8_t &b = reg[15 - k / 8];
		b = ((v >> (k - lsb)) & 1u) ? (uint8_t)(b | m) : (uint8_t)(b & ~m);
	}
}

/** @brief Seal a CID or CSD with its CRC7 and the always-1 bit. */
static void sealReg(uint8_t reg[16]) {
	reg[15] = (uint8_t)((sdp_crc7(reg, 15) << 1) | 1u);
}

/** @brief A CSD version 1.0 for an SDSC card, 5.3.2. */
static void csdV1(uint8_t csd[16], uint32_t c_size, uint32_t mult, uint32_t bl_len) {
	memset(csd, 0, 16);
	setBits(csd, 103, 96, 0x32);
	setBits(csd, 83, 80, bl_len);
	setBits(csd, 73, 62, c_size);
	setBits(csd, 49, 47, mult);
	sealReg(csd);
}

/** @brief A CSD version 2.0 for an SDHC or SDXC card, 5.3.3. */
static void csdV2(uint8_t csd[16], uint32_t c_size, uint8_t tran = 0x32) {
	memset(csd, 0, 16);
	setBits(csd, 127, 126, 1);
	setBits(csd, 103, 96, tran);
	setBits(csd, 83, 80, 9);
	setBits(csd, 69, 48, c_size);
	sealReg(csd);
}

// ---------------------------------------------------------------------------
// Wire formats.
// ---------------------------------------------------------------------------

/** @brief The specification's CRC7 examples, and frames built from them. */
static void testCrc7() {
	section("SDIO: CRC7 against the 4.5 examples");

	const uint8_t cmd0[5]  = {0x40, 0, 0, 0, 0};
	const uint8_t cmd17[5] = {0x51, 0, 0, 0, 0};
	const uint8_t resp17[5] = {0x11, 0x00, 0x00, 0x09, 0x00};
	checkInt("CMD0 argument 0", sdp_crc7(cmd0, 5), 0x4A);
	checkInt("CMD17 argument 0", sdp_crc7(cmd17, 5), 0x2A);
	checkInt("CMD17 response, status 0x900", sdp_crc7(resp17, 5), 0x33);

	// Further vectors derived here from the polynomial by long division.
	const uint8_t cmd8[5] = {0x48, 0x00, 0x00, 0x01, 0xAA};
	checkInt("CMD8 0x1AA, by long division", sdp_crc7(cmd8, 5),
	         (long)refCrc7(0x48000001AAull, 40));
	checkInt("CMD8 0x1AA is 0x43", sdp_crc7(cmd8, 5), 0x43);
	int bad = 0;
	uint64_t x = 0x0123456789ull;
	for (int i = 0; i < 2000; i++) {
		x = x * 6364136223846793005ull + 1442695040888963407ull;
		uint64_t v = (x >> 20) & 0xFFFFFFFFFFull;
		uint8_t b[5];
		for (int k = 0; k < 5; k++) b[k] = (uint8_t)(v >> (32 - 8 * k));
		if (sdp_crc7(b, 5) != refCrc7(v, 40)) bad++;
	}
	checkInt("2000 random 40-bit strings agree", bad, 0);
	checkInt("empty input", sdp_crc7(cmd0, 0), 0);
}

/** @brief CRC16: the 4.5 example, one line and four interleaved. */
static void testCrc16() {
	section("SDIO: CRC16, one line and four");

	uint8_t ff[512];
	memset(ff, 0xFF, sizeof ff);
	checkInt("512 bytes of 0xFF (4.5 example)", sdp_crc16(ff, 512), 0x7FA1);
	checkInt("same by the bitwise reference",
	         refCrc16Bits(std::vector<uint8_t>(4096, 1)), 0x7FA1);

	// The table is the polynomial G(x^4): regenerate it.
	int bad = 0;
	for (unsigned n = 0; n < 16; n++) {
		uint64_t r = (uint64_t)n << 60;
		for (int s = 0; s < 4; s++)
			r = (r >> 63) ? (r << 1) ^ 0x0001000000100001ull : (r << 1);
		uint8_t in[1] = {(uint8_t)(n << 4)};
		// Feeding nibble n then nibble 0 from a zero register applies the
		// table entry for n, shifted once more by four.
		if (sdp_crc16x4(in, 1) != (r << 4)) bad++;
	}
	checkInt("table matches x^64 + x^48 + x^20 + 1", bad, 0);

	// Four lines of a varied block against four separate bitwise CRC16s.
	uint8_t blk[512];
	for (int i = 0; i < 512; i++) blk[i] = (uint8_t)(i * 37 + (i >> 3));
	uint64_t c = sdp_crc16x4(blk, 512);
	for (unsigned j = 0; j < 4; j++) {
		char what[48];
		snprintf(what, sizeof what, "DAT%u matches its own CRC16", j);
		checkInt(what, sdp_crc16x4_line(c, j), refCrc16Bits(laneBits(blk, 512, j)));
	}
	// All 0xFF: each line is 1024 ones.
	uint64_t cf = sdp_crc16x4(ff, 512);
	uint16_t lane = refCrc16Bits(std::vector<uint8_t>(1024, 1));
	checkInt("0xFF block, DAT0", sdp_crc16x4_line(cf, 0), lane);
	checkInt("0xFF block, DAT3", sdp_crc16x4_line(cf, 3), lane);

	uint32_t w[2];
	refCrcWords(blk, 512, w);
	checkTrue("interleaved words are the wire order", sdp_rx_check(blk, 512, w));
	blk[100] ^= 0x10;
	checkTrue("one flipped bit fails the check", !sdp_rx_check(blk, 512, w));
}

/** @brief Command frames and the words the command program takes. */
static void testFraming() {
	section("SDIO: command framing");

	checkTrue("CMD0 frame is 40 00 00 00 00 95", sdp_cmd_frame(0, 0) == 0x400000000095ull);
	checkTrue("CMD17 frame is 51 00 00 00 00 55", sdp_cmd_frame(17, 0) == 0x510000000055ull);
	checkTrue("CMD8 frame is 48 00 00 01 AA 87", sdp_cmd_frame(8, 0x1AA) == 0x48000001AA87ull);
	checkTrue("index is masked to 6 bits", sdp_cmd_frame(64 + 17, 0) == sdp_cmd_frame(17, 0));

	uint32_t w[2];
	sdp_cmd_words(17, 0x12345678u, 48, w);
	checkInt("word 0 carries 47, the bit count less one", w[0] >> 24, 47);
	uint64_t f = ((uint64_t)(w[0] & 0xFFFFFFu) << 24) | (w[1] >> 8);
	checkTrue("the 48 bits are the frame", f == sdp_cmd_frame(17, 0x12345678u));
	checkInt("R1: 47 bits after the start bit, plus 8", w[1] & 0xFF, 55);
	sdp_cmd_words(2, 0, 136, w);
	checkInt("R2: 135 plus 8", w[1] & 0xFF, 143);
	sdp_cmd_words(0, 0, 0, w);
	checkInt("no response still names 55", w[1] & 0xFF, 55);

	checkInt("R1 is 48 bits", sdp_resp_bits(SDP_RESP_R1), 48);
	checkInt("R3 is 48 bits", sdp_resp_bits(SDP_RESP_R3), 48);
	checkInt("R2 is 136 bits", sdp_resp_bits(SDP_RESP_R2), 136);
	checkInt("none is 0", sdp_resp_bits(SDP_RESP_NONE), 0);
	checkInt("48-bit response is 2 words", sdp_resp_words(48), 2);
	checkInt("136-bit response is 5 words", sdp_resp_words(136), 5);
}

/** @brief Response parsing: R1, R2, R3, R6, R7, and the capture layout. */
static void testResponses() {
	section("SDIO: response parsing");

	// The CMD17 response example of 4.5, through the capture and back.
	uint8_t r1[6] = {0x11, 0x00, 0x00, 0x09, 0x00, (0x33 << 1) | 1};
	uint32_t w[5];
	uint8_t back[17];
	packResp(r1, 48, w);
	sdp_resp_unpack(w, 48, back);
	checkTrue("capture of a 48-bit response round-trips", memcmp(back, r1, 6) == 0);
	checkInt("4.5 example checks", sdp_resp48_check(back, SDP_RESP_R1, 17), SDE_OK);
	checkInt("status field", (long)sdp_resp48_arg(back), 0x900);
	checkInt("state 4, transfer", sdp_status_state(sdp_resp48_arg(back)), 4);
	checkInt("wrong index", sdp_resp48_check(back, SDP_RESP_R1, 18), SDE_FRAME);
	back[3] ^= 0x01;
	checkInt("flipped bit is a CRC error", sdp_resp48_check(back, SDP_RESP_R1, 17), SDE_CRC);
	uint8_t bad[6];
	memcpy(bad, r1, 6);
	bad[0] |= 0x40;
	checkInt("transmission bit 1 is a frame error", sdp_resp48_check(bad, SDP_RESP_R1, 17), SDE_FRAME);
	memcpy(bad, r1, 6);
	bad[5] &= 0xFE;
	checkInt("end bit 0 is a frame error", sdp_resp48_check(bad, SDP_RESP_R1, 17), SDE_FRAME);

	// R3 carries 111111 and 1111111 instead of an index and a CRC.
	uint8_t r3[6] = {0x3F, 0xC0, 0xFF, 0x80, 0x00, 0xFF};
	checkInt("R3 accepted without a CRC", sdp_resp48_check(r3, SDP_RESP_R3, 41), SDE_OK);
	checkInt("R3 OCR", (long)sdp_resp48_arg(r3), 0xC0FF8000L);
	r3[5] = 0x55;
	checkInt("R3 with a CRC field is a frame error", sdp_resp48_check(r3, SDP_RESP_R3, 41), SDE_FRAME);

	// R6: RCA on top, status bits 23, 22, 19 compressed into 15..13.
	checkInt("R6 status keeps 12..0", (long)sdp_r6_status(0xABCD0500u), 0x500);
	checkInt("R6 bit 15 is COM_CRC_ERROR", (long)sdp_r6_status(0x8000u), (long)SDP_ST_COM_CRC_ERROR);
	checkInt("R6 bit 14 is ILLEGAL_COMMAND", (long)sdp_r6_status(0x4000u), (long)SDP_ST_ILLEGAL_COMMAND);
	checkInt("R6 bit 13 is ERROR", (long)sdp_r6_status(0x2000u), (long)SDP_ST_ERROR);

	// R1 status bits.
	checkInt("clean status", sdp_status_error(0x900, 0), SDE_OK);
	checkInt("previous-command bits do not fail",
	         sdp_status_error(SDP_ST_ILLEGAL_COMMAND | SDP_ST_COM_CRC_ERROR, 0), SDE_OK);
	checkInt("WP_VIOLATION", sdp_status_error(SDP_ST_WP_VIOLATION, 0), SDE_PROTECTED);
	checkInt("OUT_OF_RANGE", sdp_status_error(SDP_ST_OUT_OF_RANGE, 0), SDE_RANGE);
	checkInt("ADDRESS_ERROR", sdp_status_error(SDP_ST_ADDRESS_ERROR, 0), SDE_RANGE);
	checkInt("ERROR", sdp_status_error(SDP_ST_ERROR, 0), SDE_STATUS);
	checkInt("ignored OUT_OF_RANGE", sdp_status_error(SDP_ST_OUT_OF_RANGE, SDP_ST_OUT_OF_RANGE), SDE_OK);

	// R2: a CSD through the capture.
	uint8_t csd[16];
	csdV2(csd, 15);
	uint8_t r2[17];
	r2[0] = 0x3F;
	memcpy(r2 + 1, csd, 16);
	packResp(r2, 136, w);
	sdp_resp_unpack(w, 136, back);
	checkTrue("capture of R2 round-trips", memcmp(back, r2, 17) == 0);
	uint8_t reg[16];
	checkInt("R2 checks", sdp_r2_check(back, reg), SDE_OK);
	checkTrue("register is the CSD", memcmp(reg, csd, 16) == 0);
	back[8] ^= 0x04;
	checkInt("R2 with a flipped bit is a CRC error", sdp_r2_check(back, reg), SDE_CRC);
	back[8] ^= 0x04;
	back[0] = 0x3E;
	checkInt("R2 without 111111 is a frame error", sdp_r2_check(back, reg), SDE_FRAME);

	// R7 echo, the way init reads it.
	uint8_t r7[6] = {0x08, 0x00, 0x00, 0x01, 0xAA, 0};
	r7[5] = (uint8_t)((sdp_crc7(r7, 5) << 1) | 1);
	checkInt("R7 checks", sdp_resp48_check(r7, SDP_RESP_R7, 8), SDE_OK);
	checkInt("R7 echo", (long)(sdp_resp48_arg(r7) & 0xFFF), 0x1AA);
}

/** @brief CSD capacity and clock, 5.3. */
static void testCsd() {
	section("SDIO: CSD capacity and TRAN_SPEED");

	uint8_t csd[16];
	// The 5.3.2 example: 32 MB with BLOCK_LEN 512, C_SIZE_MULT 3, C_SIZE 2000.
	csdV1(csd, 2000, 3, 9);
	checkInt("5.3.2 example: 2001 * 32 * 512 bytes", (long)sdp_csd_sectors(csd), 2001L * 32);
	csdV1(csd, 4095, 7, 10);
	checkInt("2 GB card with 1024-byte blocks", (long)sdp_csd_sectors(csd), 4096L * 512 * 2);
	csdV1(csd, 100, 3, 8);
	checkInt("READ_BL_LEN 8 is refused", (long)sdp_csd_sectors(csd), 0);
	csdV2(csd, 15);
	checkInt("CSD 2.0, C_SIZE 15: 8 MiB", (long)sdp_csd_sectors(csd), 16L * 1024);
	csdV2(csd, 0x3FFFFF);
	checkInt("largest C_SIZE clamps to 32 bits", (long)sdp_csd_sectors(csd), 0xFFFFFFFFL);
	setBits(csd, 127, 126, 2);
	checkInt("CSD 3.0 is refused", (long)sdp_csd_sectors(csd), 0);

	csdV2(csd, 15, 0x32);
	checkInt("TRAN_SPEED 0x32 is 25 MHz", (long)sdp_csd_max_hz(csd), 25000000L);
	csdV2(csd, 15, 0x5A);
	checkInt("TRAN_SPEED 0x5A is 50 MHz", (long)sdp_csd_max_hz(csd), 50000000L);
	csdV2(csd, 15, 0x2A);
	checkInt("TRAN_SPEED 0x2A is 20 MHz", (long)sdp_csd_max_hz(csd), 20000000L);
	csdV2(csd, 15, 0x0B);
	checkInt("TRAN_SPEED 0x0B is 100 Mbit/s", (long)sdp_csd_max_hz(csd), 100000000L);
	csdV2(csd, 15, 0x04);
	checkInt("time value 0 is reserved", (long)sdp_csd_max_hz(csd), 0);
	csdV2(csd, 15, 0x34);
	checkInt("unit 4 is reserved", (long)sdp_csd_max_hz(csd), 0);
	checkInt("field extraction", (long)sdp_reg_bits(csd, 127, 120), 0x40);
}

/** @brief The transmit stream, decoded one line at a time. */
static void testTxPack() {
	section("SDIO: four-bit packing and the transmit stream");

	uint8_t blk[512];
	for (int i = 0; i < 512; i++) blk[i] = (uint8_t)(255 - i);
	uint32_t s[SDP_TX_WORDS];
	sdp_tx_pack(blk + 0, s);
	checkInt("header count, 1044", s[0] >> 16, 1044);
	checkInt("idle, idle, idle, start", s[0] & 0xFFFF, 0xFFF0);
	checkInt("tail starts with the end bit", s[131] >> 28, 0xF);

	// Rebuild every line's bit stream from the nibbles and check each one:
	// start bit, 1024 data bits, CRC16, end bit.
	std::vector<unsigned> nib;
	for (int k = 3; k >= 0; k--) nib.push_back((s[0] >> (4 * k)) & 0xF);
	for (unsigned i = 1; i < SDP_TX_WORDS; i++)
		for (int k = 7; k >= 0; k--) nib.push_back((s[i] >> (4 * k)) & 0xF);
	nib.resize(SDP_TX_NIBBLES_M1 + 1);
	for (unsigned j = 0; j < 4; j++) {
		std::vector<uint8_t> line;
		for (unsigned v : nib) line.push_back((v >> j) & 1u);
		bool ok = line[0] && line[1] && line[2] && line[3] == 0 && line.back() == 1;
		std::vector<uint8_t> data(line.begin() + 4, line.begin() + 4 + 1024);
		ok = ok && data == laneBits(blk, 512, j);
		uint16_t crc = 0;
		for (int m = 0; m < 16; m++) crc = (uint16_t)((crc << 1) | line[4 + 1024 + m]);
		ok = ok && crc == refCrc16Bits(data);
		char what[48];
		snprintf(what, sizeof what, "DAT%u: start, data, CRC16, end", j);
		checkTrue(what, ok);
	}

	// Byte order: byte 0's high nibble is the first data nibble.
	checkInt("first data word is bytes 0..3", (long)s[1], 0xFFFEFDFCL);
	// An odd source address packs the same bytes.
	static uint8_t shifted[513];
	memcpy(shifted + 1, blk, 512);
	uint32_t s2[SDP_TX_WORDS];
	sdp_tx_pack(shifted + 1, s2);
	checkTrue("unaligned source packs the same", memcmp(s, s2, sizeof s) == 0);
}

/** @brief The CRC status token in the window after a written block. */
static void testCrcStatus() {
	section("SDIO: write CRC status token");

	// Nibbles: DAT0 is bit 0. Others float high.
	auto window = [](const char *d0, uint32_t w[2]) {
		w[0] = w[1] = 0;
		for (unsigned k = 0; k < 16; k++) {
			unsigned nib = 0xE | (unsigned)(d0[k] == '1');
			w[k / 8] |= (uint32_t)nib << (28 - 4 * (k % 8));
		}
	};
	uint32_t w[2];
	window("1100101000000000", w);
	checkInt("010 after two idle clocks: accepted", sdp_crc_status(w), 2);
	window("1101011000000000", w);
	checkInt("101: CRC error", sdp_crc_status(w), 5);
	window("1011010000000000", w);
	checkInt("110: write error", sdp_crc_status(w), 6);
	window("0010100000000000", w);
	checkInt("token in the first clock", sdp_crc_status(w), 2);
	window("1111111111100101", w);
	checkInt("token as late as it fits", sdp_crc_status(w), 2);
	window("1111111111111010", w);
	checkInt("token cut off by the window", sdp_crc_status(w), -1);
	window("1111111111111111", w);
	checkInt("no token at all", sdp_crc_status(w), -1);
	window("1100100000000000", w);
	checkInt("no end bit", sdp_crc_status(w), -1);
}

// ---------------------------------------------------------------------------
// The simulated card.
// ---------------------------------------------------------------------------

/** @brief A card that behaves as section 4 describes, plus faults on demand. */
struct SimCard {
	enum Kind { NONE, V1_SDSC, V2_SDSC, SDHC } kind = SDHC;
	enum State { IDLE, READY, IDENT, STBY, TRAN, DATA, RCV, PRG, INA };

	// Configuration.
	uint32_t window   = 0x00FF8000u; // 2.7 V to 3.6 V
	int      polls    = 3;           // ACMD41 calls answered busy
	bool     badEcho  = false;
	bool     answerThenInactive = false;
	int      csdVer   = -1;          // force a CSD_STRUCTURE
	uint8_t  tran     = 0x32;
	uint32_t rangeLimit = 0xFFFFFFFFu; // reads at or past this: OUT_OF_RANGE

	// Faults.
	int  corruptResp   = 0;   // responses delivered with a flipped bit
	int  corruptData   = 0;   // read blocks delivered with a flipped bit
	int  crcRejects    = 0;   // written blocks answered 101
	int  writeRejects  = 0;   // written blocks answered 110
	bool noData        = false;
	bool busyForever   = false;
	int  removeAtCmd   = -1;  // stop answering from this command on
	int  zeroRca       = 0;   // CMD3 answers with RCA 0 this many times
	int  badFrameCmd   = -1;  // this command's response has its end bit 0
	bool r6Error       = false; // CMD3 reports ERROR
	int  forceState    = -1;  // CMD13 reports this state
	bool stopSilent    = false; // CMD12 gets no response
	bool armFail       = false; // the receiver cannot be armed
	bool noToken       = false; // no CRC status token after a block
	int  wpOnStatus    = 0;   // CMD13 reports WP_VIOLATION this many times

	// State.
	State    state = IDLE;
	bool     removed = false, appCmd = false, wide = false, ccs = false;
	bool     illegalPrev = false;
	uint16_t rca = 0;
	uint32_t blockLen = 512;
	uint8_t  cid[16], csd[16];
	uint32_t t = 0, busyUntil = 0, clockHz = 0;
	bool     clockedBeforeCmd0 = false;
	int      cmdCount = 0, frameErrors = 0, fastIdent = 0, stops = 0;
	std::vector<int> log;
	std::map<uint32_t, std::vector<uint8_t>> mem;
	uint32_t writeAddr = 0;
	bool     writeMulti = false;

	// Receiver as armed by the host.
	uint8_t  *rxBuf = nullptr;
	uint32_t  rxBytes = 0, rxBlocks = 0, *rxCrc = nullptr;
	bool      rxArmed = false, rxDone = false;
	int       unalignedArms = 0;

	SimCard() {
		memset(cid, 0, 16);
		cid[0] = 0x03; cid[1] = 'S'; cid[2] = 'D';
		memcpy(cid + 3, "SIM01", 5);
		cid[8] = 0x10;
		sealReg(cid);
	}

	uint32_t sectors() const {
		return kind == SDHC ? 16u * 1024u : 2001u * 32u;
	}

	/** @brief Contents of a sector: written data, or a pattern of its number. */
	std::vector<uint8_t> sector(uint32_t lba) {
		auto it = mem.find(lba);
		if (it != mem.end()) return it->second;
		std::vector<uint8_t> v(512);
		for (int i = 0; i < 512; i++) v[i] = (uint8_t)(lba * 7 + i * 13 + (i >> 5));
		return v;
	}

	void buildCsd() {
		if (kind == SDHC) csdV2(csd, 15, tran);
		else csdV1(csd, 2000, 3, 9);
		if (csdVer >= 0) { setBits(csd, 127, 126, (uint32_t)csdVer); sealReg(csd); }
	}

	uint32_t status() {
		uint32_t s = (uint32_t)(forceState >= 0 ? forceState : state == PRG ? TRAN : state) << 9 |
		             SDP_ST_READY_FOR_DATA;
		if (appCmd) s |= SDP_ST_APP_CMD;
		if (illegalPrev) s |= SDP_ST_ILLEGAL_COMMAND;
		illegalPrev = false;
		return s;
	}

	/** @brief Turn a canonical response into captured words and back. */
	void respond(uint8_t *out, const uint8_t *r, unsigned bits) {
		uint8_t tmp[17];
		memcpy(tmp, r, bits / 8);
		if (corruptResp > 0) { tmp[2] ^= 0x20; corruptResp--; }
		if (cmdCount == badFrameCmd) tmp[bits / 8 - 1] &= 0xFE;
		uint32_t w[5];
		packResp(tmp, bits, w);
		sdp_resp_unpack(w, bits, out);
	}

	void r48(uint8_t *out, uint8_t index, uint32_t arg, bool r3 = false) {
		uint8_t r[6] = {(uint8_t)(r3 ? 0x3F : index), (uint8_t)(arg >> 24),
		                (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg, 0xFF};
		if (!r3) r[5] = (uint8_t)((sdp_crc7(r, 5) << 1) | 1);
		respond(out, r, 48);
	}

	void r2(uint8_t *out, const uint8_t reg[16]) {
		uint8_t r[17];
		r[0] = 0x3F;
		memcpy(r + 1, reg, 16);
		respond(out, r, 136);
	}

	/** @brief One block as the DMA leaves it: data swapped into place, CRC raw. */
	void deliver(const uint8_t *data, uint32_t len, uint32_t blk) {
		std::vector<unsigned> nib;
		for (uint32_t i = 0; i < len; i++) { nib.push_back(data[i] >> 4); nib.push_back(data[i] & 0xF); }
		uint32_t crc[2];
		refCrcWords(data, len, crc);
		for (unsigned k = 0; k < 16; k++) nib.push_back((crc[k / 8] >> (28 - 4 * (k % 8))) & 0xF);
		std::vector<uint32_t> fifo;
		for (size_t k = 0; k < nib.size(); k += 8) {
			uint32_t w = 0;
			for (int i = 0; i < 8; i++) w = (w << 4) | nib[k + i];
			fifo.push_back(w);
		}
		uint8_t *dst = rxBuf + blk * len;
		for (uint32_t i = 0; i < len / 4; i++) {
			uint32_t v = __builtin_bswap32(fifo[i]);
			memcpy(dst + 4 * i, &v, 4);
		}
		rxCrc[2 * blk]     = fifo[len / 4];
		rxCrc[2 * blk + 1] = fifo[len / 4 + 1];
		if (corruptData > 0) { dst[len / 2] ^= 0x01; corruptData--; }
	}

	sd_err_t startRead(uint32_t arg, bool multi, uint8_t *out, uint8_t index) {
		uint32_t lba = (kind == SDHC) ? arg : arg / 512u;
		if (kind != SDHC && (arg % blockLen)) {
			r48(out, index, status() | SDP_ST_ADDRESS_ERROR);
			return SDE_OK;
		}
		if (lba >= sectors() || lba >= rangeLimit) {
			r48(out, index, status() | SDP_ST_OUT_OF_RANGE);
			return SDE_OK;
		}
		r48(out, index, status());
		state = multi ? DATA : TRAN;
		if (noData || !rxArmed || rxBytes != 512) return SDE_OK;
		uint32_t n = multi ? rxBlocks : 1;
		for (uint32_t b = 0; b < n; b++) deliver(sector(lba + b).data(), 512, b);
		rxDone = true;
		return SDE_OK;
	}

	sd_err_t command(uint8_t index, uint32_t arg, unsigned bits, uint8_t *out) {
		// Every frame is checked the way a card's receiver would check it.
		uint32_t w[2];
		sdp_cmd_words(index, arg, bits, w);
		uint64_t f = ((uint64_t)(w[0] & 0xFFFFFFu) << 24) | (w[1] >> 8);
		unsigned rx = (bits ? bits : 48) - 1 + SDP_TRAIL_CLOCKS;
		if ((w[0] >> 24) != 47 || (w[1] & 0xFF) != rx || ((f >> 47) & 1) || !((f >> 46) & 1) ||
		    !(f & 1) || ((f >> 1) & 0x7F) != refCrc7(f >> 8, 40) ||
		    ((f >> 40) & 0x3F) != index || (uint32_t)(f >> 8) != arg)
			frameErrors++;

		log.push_back(appCmd ? 100 + index : index);
		cmdCount++;
		t += 100;
		if (removeAtCmd >= 0 && cmdCount > removeAtCmd) removed = true;
		bool app = appCmd;
		appCmd = false;
		// A command without a response cannot fail on the bus: nobody answers.
		if (removed || kind == NONE || state == INA) return bits ? silence() : SDE_OK;
		if (state <= IDENT && clockHz > 400000u) fastIdent++;

		switch (app ? 100 + index : index) {
			case 0:
				state = IDLE; wide = false; ccs = false; rca = 0;
				return SDE_OK;
			case 8:
				if (state != IDLE || kind == V1_SDSC) { illegalPrev = true; return silence(); }
				r48(out, 8, badEcho ? (arg & 0xF00u) | 0x55u : (arg & 0xFFFu));
				return SDE_OK;
			case 55:
				if (state >= STBY && (arg >> 16) != rca) return silence();
				appCmd = true;
				r48(out, 55, status());
				return SDE_OK;
			case 141: {
				if (state != IDLE && state != READY) { illegalPrev = true; return silence(); }
				uint32_t w24 = arg & 0x00FFFFFFu;
				if (w24 && !(w24 & window)) {
					if (answerThenInactive) { state = INA; r48(out, 41, window, true); return SDE_OK; }
					state = INA;
					return silence();
				}
				uint32_t ocr = window;
				bool hcs = (arg & SDP_OCR_CCS) != 0;
				bool canFinish = kind != SDHC || hcs;
				if (polls-- <= 0 && canFinish) {
					state = READY;
					ccs = kind == SDHC;
					ocr |= SDP_OCR_BUSY | (ccs ? SDP_OCR_CCS : 0u);
				}
				r48(out, 41, ocr, true);
				return SDE_OK;
			}
			case 2:
				if (state != READY) return silence();
				state = IDENT;
				r2(out, cid);
				return SDE_OK;
			case 3:
				if (state != IDENT && state != STBY) return silence();
				state = STBY;
				rca = zeroRca > 0 ? 0 : 0xB0B0;
				if (zeroRca > 0) zeroRca--;
				r48(out, 3, (uint32_t)rca << 16 | (status() & 0x1FFFu) | (r6Error ? 0x2000u : 0u));
				return SDE_OK;
			case 9:
				if (state != STBY || (arg >> 16) != rca) return silence();
				buildCsd();
				r2(out, csd);
				return SDE_OK;
			case 7:
				if ((arg >> 16) != rca) { state = STBY; return silence(); }
				r48(out, 7, status());
				state = TRAN;
				busyUntil = t + 50;
				return SDE_OK;
			case 106:
				if (state != TRAN) return silence();
				wide = (arg & 3u) == 2u;
				r48(out, 6, status());
				return SDE_OK;
			case 16:
				if (state != TRAN) return silence();
				blockLen = arg;
				r48(out, 16, status());
				return SDE_OK;
			case 13:
				if ((arg >> 16) != rca) return silence();
				r48(out, 13, status() | (wpOnStatus > 0 ? SDP_ST_WP_VIOLATION : 0u));
				if (wpOnStatus > 0) wpOnStatus--;
				return SDE_OK;
			case 113: {
				if (state != TRAN) return silence();
				r48(out, 13, status());
				if (noData || !rxArmed || rxBytes != 64) return SDE_OK;
				uint8_t sds[64];
				for (int i = 0; i < 64; i++) sds[i] = (uint8_t)(i ^ 0x5A);
				deliver(sds, 64, 0);
				rxDone = true;
				return SDE_OK;
			}
			case 17:
			case 18:
				if (state != TRAN || !wide) return silence();
				return startRead(arg, index == 18, out, index);
			case 12:
				if (stopSilent || (state != DATA && state != RCV)) { illegalPrev = true; return silence(); }
				r48(out, 12, status());
				state = TRAN;
				busyUntil = t + 30;
				return SDE_OK;
			case 24:
			case 25:
				if (state != TRAN || !wide) return silence();
				writeAddr = kind == SDHC ? arg : arg / 512u;
				writeMulti = index == 25;
				r48(out, index, status());
				state = RCV;
				return SDE_OK;
			default:
				illegalPrev = true;
				return silence();
		}
	}

	sd_err_t silence() {
		t += SDH_CMD_TIMEOUT_US;
		return SDE_NO_RESPONSE;
	}

	sd_err_t txBlock(const uint32_t *words, uint32_t n, uint32_t st[2], uint32_t timeout) {
		if (removeAtCmd >= 0 && cmdCount >= removeAtCmd) removed = true;
		if (removed || state != RCV || n != SDP_TX_WORDS) {
			t += timeout;
			return SDE_TIMEOUT;
		}
		t += 60;
		uint8_t data[512];
		for (int i = 0; i < 128; i++)
			for (int k = 0; k < 4; k++) data[4 * i + k] = (uint8_t)(words[1 + i] >> (24 - 8 * k));
		uint32_t crc[2];
		refCrcWords(data, 512, crc);
		bool framed = (words[0] >> 16) == SDP_TX_NIBBLES_M1 && (words[0] & 0xFFFF) == 0xFFF0 &&
		              (words[131] >> 28) == 0xF;
		unsigned tok;
		if (!framed || crc[0] != words[129] || crc[1] != words[130] || crcRejects > 0) {
			if (crcRejects > 0) crcRejects--;
			tok = 5;
		} else if (writeRejects > 0) {
			writeRejects--;
			tok = 6;
		} else {
			mem[writeAddr++] = std::vector<uint8_t>(data, data + 512);
			tok = 2;
		}
		// Two idle clocks, the token, then busy.
		const unsigned d0[16] = {1, 1, 0, (tok >> 2) & 1, (tok >> 1) & 1, tok & 1, 1,
		                         0, 0, 0, 0, 0, 0, 0, 0, 0};
		st[0] = st[1] = 0;
		for (unsigned k = 0; k < 16; k++)
			st[k / 8] |= (uint32_t)(0xE | (noToken ? 1u : d0[k])) << (28 - 4 * (k % 8));
		busyUntil = busyForever ? 0xFFFFFFFFu : t + 300;
		// A failed single block returns to transfer; a failed multi-block
		// write ignores the rest and waits for CMD12 (4.3.3, 4.3.4).
		if (tok != 2) state = writeMulti ? RCV : TRAN;
		else if (!writeMulti) state = TRAN;
		return SDE_OK;
	}
};

// Bus adapters: the simulated card behind the sdh_bus_t table.
static sd_err_t simCommand(void *c, uint8_t i, uint32_t a, unsigned b, uint8_t *r) {
	return ((SimCard *)c)->command(i, a, b, r);
}
static uint32_t simSetClock(void *c, uint32_t hz) { return ((SimCard *)c)->clockHz = hz; }
static void simRunClock(void *c, uint32_t us) {
	SimCard *s = (SimCard *)c;
	s->t += us;
	if (s->cmdCount == 0 && s->clockHz <= 400000u && us >= 1000u) s->clockedBeforeCmd0 = true;
}
static void simStopClock(void *c) { ((SimCard *)c)->stops++; }
static sd_err_t simRxArm(void *c, uint8_t *buf, uint32_t bytes, uint32_t blocks, uint32_t *crc) {
	SimCard *s = (SimCard *)c;
	if (s->armFail) return SDE_HW;
	if ((uintptr_t)buf & 3u) s->unalignedArms++;
	s->rxBuf = buf; s->rxBytes = bytes; s->rxBlocks = blocks; s->rxCrc = crc;
	s->rxArmed = true; s->rxDone = false;
	return SDE_OK;
}
static sd_err_t simRxWait(void *c, uint32_t timeout) {
	SimCard *s = (SimCard *)c;
	if (s->rxDone) return SDE_OK;
	s->t += timeout;
	return SDE_TIMEOUT;
}
static void simRxStop(void *c) { ((SimCard *)c)->rxArmed = false; }
static sd_err_t simTx(void *c, const uint32_t *w, uint32_t n, uint32_t st[2], uint32_t to) {
	return ((SimCard *)c)->txBlock(w, n, st, to);
}
static bool simDat0(void *c) {
	SimCard *s = (SimCard *)c;
	return s->removed || s->t >= s->busyUntil;
}
static uint32_t simNow(void *c) { return ((SimCard *)c)->t; }
static void simSleep(void *c, uint32_t us) { ((SimCard *)c)->t += us; }

static sdh_bus_t busOf(SimCard *s) {
	sdh_bus_t b;
	b.ctx = s;
	b.command = simCommand;
	b.set_clock = simSetClock;
	b.run_clock = simRunClock;
	b.stop_clock = simStopClock;
	b.rx_arm = simRxArm;
	b.rx_wait = simRxWait;
	b.rx_stop = simRxStop;
	b.tx_block = simTx;
	b.dat0 = simDat0;
	b.now_us = simNow;
	b.sleep_us = simSleep;
	return b;
}

/** @brief How many times command @p index went out (ACMDs as 100 + index). */
static int count(const SimCard &s, int index) {
	int n = 0;
	for (int v : s.log) n += v == index;
	return n;
}

// ---------------------------------------------------------------------------
// The protocol against the simulated card.
// ---------------------------------------------------------------------------

/** @brief Initialisation of the three card types. */
static void testInitTypes() {
	section("SDIO: initialisation, SDHC, SDSC v2, SDSC v1");

	SimCard hc;
	sdh_bus_t b = busOf(&hc);
	sdh_card_t c;
	checkInt("SDHC initialises", sdh_init(&b, &c, 10000000u), SDE_OK);
	checkTrue("ready", c.ready);
	checkTrue("answered CMD8", c.v2);
	checkTrue("block addressing", c.block_addr);
	checkInt("RCA", c.rca, 0xB0B0);
	checkInt("capacity from CSD 2.0", (long)c.sectors, 16384);
	checkInt("data clock as asked", (long)c.clock_hz, 10000000L);
	checkTrue("CID kept", memcmp(c.cid, hc.cid, 16) == 0);
	checkTrue("1 ms of clock before CMD0", hc.clockedBeforeCmd0);
	checkInt("no command above 400 kHz before transfer state", hc.fastIdent, 0);
	checkTrue("four-bit bus selected", hc.wide);
	checkInt("no CMD16 for SDHC", count(hc, 16), 0);
	checkInt("ACMD41 polled until ready", count(hc, 141), 4);
	checkInt("every frame well formed", hc.frameErrors, 0);
	checkInt("card in transfer state", hc.state, SimCard::TRAN);

	SimCard v2;
	v2.kind = SimCard::V2_SDSC;
	b = busOf(&v2);
	checkInt("SDSC v2 initialises", sdh_init(&b, &c, 10000000u), SDE_OK);
	checkTrue("v2", c.v2);
	checkTrue("byte addressing", !c.block_addr);
	checkInt("capacity from CSD 1.0", (long)c.sectors, 2001L * 32);
	checkInt("CMD16 sets 512", count(v2, 16), 1);
	checkInt("block length", (long)v2.blockLen, 512);

	SimCard v1;
	v1.kind = SimCard::V1_SDSC;
	b = busOf(&v1);
	checkInt("SDSC v1 initialises", sdh_init(&b, &c, 10000000u), SDE_OK);
	checkTrue("not v2", !c.v2);
	checkTrue("byte addressing", !c.block_addr);
	checkInt("HCS left clear without CMD8", (long)(c.ocr & SDP_OCR_CCS), 0);

	SimCard slow;
	slow.tran = 0x2A;   // 20 MHz
	b = busOf(&slow);
	checkInt("card with TRAN_SPEED 20 MHz", sdh_init(&b, &c, 25000000u), SDE_OK);
	checkInt("clock capped at the card's rate", (long)c.clock_hz, 20000000L);
	SimCard fast;
	fast.tran = 0x5A;   // 50 MHz
	b = busOf(&fast);
	sdh_init(&b, &c, 50000000u);
	checkInt("clock capped at 25 MHz default speed", (long)c.clock_hz, 25000000L);

	SimCard zr;
	zr.zeroRca = 1;
	b = busOf(&zr);
	checkInt("RCA 0 is asked again", sdh_init(&b, &c, 10000000u), SDE_OK);
	checkInt("CMD3 twice", count(zr, 3), 2);
	SimCard zr3;
	zr3.zeroRca = 5;
	b = busOf(&zr3);
	checkInt("RCA 0 three times is unusable", sdh_init(&b, &c, 10000000u), SDE_UNUSABLE);
}

/** @brief Initialisation failures: none of them may hang. */
static void testInitFailures() {
	section("SDIO: initialisation failures");

	SimCard none;
	none.kind = SimCard::NONE;
	sdh_bus_t b = busOf(&none);
	sdh_card_t c;
	checkInt("no card: no response", sdh_init(&b, &c, 10000000u), SDE_NO_RESPONSE);
	checkTrue("not ready", !c.ready);
	checkTrue("gave up within 20 ms", none.t < 20000u);

	SimCard volt;
	volt.window = 0x00018000u;   // 2.7 V to 2.9 V only
	b = busOf(&volt);
	checkInt("voltage mismatch, card goes silent", sdh_init(&b, &c, 10000000u), SDE_VOLTAGE);
	SimCard volt2;
	volt2.window = 0x00018000u;
	volt2.answerThenInactive = true;
	b = busOf(&volt2);
	checkInt("voltage mismatch, card answers first", sdh_init(&b, &c, 10000000u), SDE_VOLTAGE);

	SimCard stuck;
	stuck.polls = 1000000;
	b = busOf(&stuck);
	checkInt("never ready: timeout", sdh_init(&b, &c, 10000000u), SDE_TIMEOUT);
	checkTrue("after more than 1 s of ACMD41", stuck.t >= 1000000u);
	checkTrue("and not much more", stuck.t < 1300000u);

	SimCard echo;
	echo.badEcho = true;
	b = busOf(&echo);
	checkInt("CMD8 echo wrong: unusable", sdh_init(&b, &c, 10000000u), SDE_UNUSABLE);

	SimCard sduc;
	sduc.csdVer = 2;
	b = busOf(&sduc);
	checkInt("CSD 3.0: unusable", sdh_init(&b, &c, 10000000u), SDE_UNUSABLE);

	SimCard crc;
	crc.corruptResp = 1;
	b = busOf(&crc);
	checkInt("damaged CMD8 response fails init", sdh_init(&b, &c, 10000000u), SDE_CRC);

	SimCard gone;
	gone.removeAtCmd = 6;   // after CMD0, CMD8, two ACMD41 rounds
	b = busOf(&gone);
	checkInt("card pulled during init", sdh_init(&b, &c, 10000000u), SDE_NO_RESPONSE);

	for (int at = 6; at <= 16; at++) {
		SimCard late;
		late.removeAtCmd = at;
		b = busOf(&late);
		sd_err_t e = sdh_init(&b, &c, 10000000u);
		char what[48];
		snprintf(what, sizeof what, "pulled after command %d fails", at);
		checkTrue(what, e != SDE_OK && !c.ready);
	}
}

/** @brief Single and multi-block reads, aligned and not. */
static void testRead() {
	section("SDIO: reads");

	SimCard s;
	sdh_bus_t b = busOf(&s);
	sdh_card_t c;
	sdh_init(&b, &c, 10000000u);

	static uint32_t buf32[40 * 128 + 1];
	uint8_t *buf = (uint8_t *)buf32;
	s.log.clear();
	checkInt("one block", sdh_read(&b, &c, 5, 1, buf), SDE_OK);
	checkTrue("data right", memcmp(buf, s.sector(5).data(), 512) == 0);
	checkInt("CMD17", count(s, 17), 1);
	checkInt("no CMD12 after CMD17", count(s, 12), 0);

	s.log.clear();
	checkInt("eight blocks", sdh_read(&b, &c, 100, 8, buf), SDE_OK);
	bool ok = true;
	for (int i = 0; i < 8; i++) ok = ok && memcmp(buf + 512 * i, s.sector(100 + i).data(), 512) == 0;
	checkTrue("data right in every block", ok);
	checkInt("one CMD18", count(s, 18), 1);
	checkInt("one CMD12", count(s, 12), 1);

	s.log.clear();
	checkInt("forty blocks", sdh_read(&b, &c, 200, 40, buf), SDE_OK);
	ok = true;
	for (int i = 0; i < 40; i++) ok = ok && memcmp(buf + 512 * i, s.sector(200 + i).data(), 512) == 0;
	checkTrue("data right across the split", ok);
	checkInt("split into 32 and 8", count(s, 18), 2);

	s.log.clear();
	s.unalignedArms = 0;
	uint8_t *odd = buf + 1;
	checkInt("unaligned buffer, three blocks", sdh_read(&b, &c, 7, 3, odd), SDE_OK);
	ok = true;
	for (int i = 0; i < 3; i++) ok = ok && memcmp(odd + 512 * i, s.sector(7 + i).data(), 512) == 0;
	checkTrue("data right", ok);
	checkInt("one CMD17 per block", count(s, 17), 3);
	checkInt("receiver never armed unaligned", s.unalignedArms, 0);

	checkInt("last sector", sdh_read(&b, &c, c.sectors - 1, 1, buf), SDE_OK);
	s.log.clear();
	checkInt("past the end: refused", sdh_read(&b, &c, c.sectors - 1, 2, buf), SDE_PARAM);
	checkInt("zero sectors: refused", sdh_read(&b, &c, 0, 0, buf), SDE_PARAM);
	checkInt("null buffer: refused", sdh_read(&b, &c, 0, 1, nullptr), SDE_PARAM);
	checkInt("nothing sent for refused calls", (long)s.log.size(), 0);

	SimCard v1;
	v1.kind = SimCard::V1_SDSC;
	b = busOf(&v1);
	sdh_init(&b, &c, 10000000u);
	checkInt("SDSC read by byte address", sdh_read(&b, &c, 3, 2, buf), SDE_OK);
	checkTrue("SDSC data right", memcmp(buf, v1.sector(3).data(), 512) == 0 &&
	                             memcmp(buf + 512, v1.sector(4).data(), 512) == 0);
}

/** @brief Read errors: CRC, timeouts, the card's own verdict. */
static void testReadErrors() {
	section("SDIO: read errors");

	static uint32_t buf32[8 * 128];
	uint8_t *buf = (uint8_t *)buf32;
	SimCard s;
	sdh_bus_t b = busOf(&s);
	sdh_card_t c;
	sdh_init(&b, &c, 10000000u);

	s.corruptData = 1;
	s.log.clear();
	checkInt("one damaged block is read again", sdh_read(&b, &c, 1, 4, buf), SDE_OK);
	checkInt("second CMD18", count(s, 18), 2);
	checkTrue("data right", memcmp(buf, s.sector(1).data(), 512) == 0);

	s.corruptData = 3;
	checkInt("damaged three times: CRC error", sdh_read(&b, &c, 1, 1, buf), SDE_CRC);
	checkTrue("card still ready after a CRC error", c.ready);

	s.corruptResp = 1;
	checkInt("damaged response is retried", sdh_read(&b, &c, 1, 1, buf), SDE_OK);

	s.rangeLimit = 50;
	s.log.clear();
	checkInt("card says OUT_OF_RANGE", sdh_read(&b, &c, 60, 1, buf), SDE_RANGE);
	checkInt("not retried", count(s, 17), 1);
	checkInt("no CMD12 for a refused command", count(s, 12), 0);
	s.rangeLimit = 0xFFFFFFFFu;

	s.noData = true;
	s.log.clear();
	uint32_t t0 = s.t;
	checkInt("data never comes: timeout", sdh_read(&b, &c, 1, 2, buf), SDE_TIMEOUT);
	checkInt("three attempts", count(s, 18), 3);
	checkTrue("each waited 250 ms", s.t - t0 >= 3u * SDH_READ_TIMEOUT_US);
	checkTrue("card marked not ready", !c.ready);
	checkInt("then refused at once", sdh_read(&b, &c, 1, 1, buf), SDE_NOT_READY);

	SimCard gone;
	b = busOf(&gone);
	sdh_init(&b, &c, 10000000u);
	gone.removed = true;
	uint32_t t1 = gone.t;
	checkInt("card pulled: no response", sdh_read(&b, &c, 1, 8, buf), SDE_NO_RESPONSE);
	checkTrue("within 20 ms", gone.t - t1 < 20000u);
	checkTrue("not ready", !c.ready);
}

/** @brief Writes, through programming. */
static void testWrite() {
	section("SDIO: writes");

	SimCard s;
	sdh_bus_t b = busOf(&s);
	sdh_card_t c;
	sdh_init(&b, &c, 10000000u);

	static uint8_t data[10 * 512 + 1];
	for (int i = 0; i < (int)sizeof data; i++) data[i] = (uint8_t)(i * 29 + 3);

	s.log.clear();
	checkInt("one block", sdh_write(&b, &c, 9, 1, data), SDE_OK);
	checkTrue("card holds it", memcmp(s.sector(9).data(), data, 512) == 0);
	checkInt("CMD24", count(s, 24), 1);
	checkInt("no CMD12 after CMD24", count(s, 12), 0);
	checkInt("status read after programming", count(s, 13), 1);
	checkInt("card back in transfer state", s.state, SimCard::TRAN);

	s.log.clear();
	checkInt("ten blocks from an odd address", sdh_write(&b, &c, 30, 10, data + 1), SDE_OK);
	bool ok = true;
	for (int i = 0; i < 10; i++) ok = ok && memcmp(s.sector(30 + i).data(), data + 1 + 512 * i, 512) == 0;
	checkTrue("card holds all ten", ok);
	checkInt("one CMD25", count(s, 25), 1);
	checkInt("one CMD12", count(s, 12), 1);

	static uint8_t big[40 * 512];
	for (int i = 0; i < (int)sizeof big; i++) big[i] = (uint8_t)(i ^ (i >> 9));
	s.log.clear();
	checkInt("forty blocks, split", sdh_write(&b, &c, 2000, 40, big), SDE_OK);
	checkInt("split into 32 and 8", count(s, 25), 2);
	checkTrue("last block landed", memcmp(s.sector(2039).data(), big + 39 * 512, 512) == 0);

	SimCard v1;
	v1.kind = SimCard::V1_SDSC;
	b = busOf(&v1);
	sdh_init(&b, &c, 10000000u);
	checkInt("SDSC write by byte address", sdh_write(&b, &c, 12, 2, data), SDE_OK);
	checkTrue("SDSC card holds both", memcmp(v1.sector(12).data(), data, 512) == 0 &&
	                                  memcmp(v1.sector(13).data(), data + 512, 512) == 0);

	// The round trip the logger and the card reader make between them.
	SimCard rt;
	b = busOf(&rt);
	sdh_init(&b, &c, 25000000u);
	static uint32_t back32[10 * 128];
	sdh_write(&b, &c, 500, 10, data);
	checkInt("read back", sdh_read(&b, &c, 500, 10, (uint8_t *)back32), SDE_OK);
	checkTrue("what was written", memcmp(back32, data, 10 * 512) == 0);
}

/** @brief Write errors: CRC status, write error, busy, removal. */
static void testWriteErrors() {
	section("SDIO: write CRC status and busy");

	static uint8_t data[4 * 512];
	memset(data, 0xA5, sizeof data);
	SimCard s;
	sdh_bus_t b = busOf(&s);
	sdh_card_t c;
	sdh_init(&b, &c, 10000000u);

	s.crcRejects = 1;
	s.log.clear();
	checkInt("one 101 token: written again", sdh_write(&b, &c, 40, 4, data), SDE_OK);
	checkInt("CMD25 twice", count(s, 25), 2);
	checkInt("CMD12 after each", count(s, 12), 2);
	checkTrue("all four blocks landed", memcmp(s.sector(43).data(), data, 512) == 0);

	s.crcRejects = 3;
	checkInt("three 101 tokens: CRC error", sdh_write(&b, &c, 40, 1, data), SDE_CRC);
	checkTrue("still ready", c.ready);

	s.writeRejects = 1;
	s.log.clear();
	checkInt("110 token: write error", sdh_write(&b, &c, 40, 2, data), SDE_WRITE);
	checkInt("not retried", count(s, 25), 1);
	checkInt("stopped with CMD12", count(s, 12), 1);

	s.busyForever = true;
	uint32_t t0 = s.t;
	checkInt("busy never ends: timeout", sdh_write(&b, &c, 40, 1, data), SDE_TIMEOUT);
	checkTrue("waited 1 s per attempt", s.t - t0 >= SDH_BUSY_TIMEOUT_US);
	checkTrue("card marked not ready", !c.ready);

	SimCard gone;
	b = busOf(&gone);
	sdh_init(&b, &c, 10000000u);
	gone.removeAtCmd = gone.cmdCount + 1;   // answers CMD25, then nothing
	checkTrue("pulled during a write fails",
	          sdh_write(&b, &c, 40, 3, data) != SDE_OK && !c.ready);
	checkTrue("DAT0 floats high, so no busy wait hangs", gone.t < 10000000u);
}

/** @brief SD Status, presence and the error classes. */
static void testStatusAndProbe() {
	section("SDIO: SD Status, presence, error classes");

	SimCard s;
	sdh_bus_t b = busOf(&s);
	sdh_card_t c;
	uint8_t sds[64];
	memset(&c, 0, sizeof c);
	checkInt("SD Status before init", sdh_sd_status(&b, &c, sds), SDE_NOT_READY);
	checkTrue("probe finds an uninitialised card", sdh_probe(&b, &c));
	sdh_init(&b, &c, 10000000u);
	checkInt("ACMD13", sdh_sd_status(&b, &c, sds), SDE_OK);
	checkInt("first byte", sds[0], 0x5A);
	checkInt("last byte", sds[63], 63 ^ 0x5A);
	s.corruptData = 1;
	checkInt("damaged SD Status", sdh_sd_status(&b, &c, sds), SDE_CRC);
	s.noData = true;
	checkInt("SD Status that never comes", sdh_sd_status(&b, &c, sds), SDE_TIMEOUT);
	checkTrue("marks the card not ready", !c.ready);

	sdh_init(&b, &c, 10000000u);
	s.noData = false;
	checkTrue("probe finds a ready card", sdh_probe(&b, &c));
	uint32_t st = 0;
	checkInt("CMD13", sdh_status(&b, &c, &st), SDE_OK);
	checkInt("in transfer state", sdp_status_state(st), SDP_STATE_TRAN);
	s.removed = true;
	checkTrue("probe after removal", !sdh_probe(&b, &c));
	checkTrue("marks the card not ready", !c.ready);
	checkTrue("probe of an empty slot", !sdh_probe(&b, &c));
	SimCard v1;
	v1.kind = SimCard::V1_SDSC;
	b = busOf(&v1);
	memset(&c, 0, sizeof c);
	checkTrue("probe finds a v1 card by CMD55", sdh_probe(&b, &c));

	checkTrue("no response is fatal", sdh_err_is_fatal(SDE_NO_RESPONSE));
	checkTrue("timeout is fatal", sdh_err_is_fatal(SDE_TIMEOUT));
	checkTrue("CRC is not", !sdh_err_is_fatal(SDE_CRC));
	checkTrue("a status error is not", !sdh_err_is_fatal(SDE_STATUS));
}

/** @brief Every other way a step can fail. */
static void testFaultPaths() {
	section("SDIO: remaining failure paths");

	static uint32_t buf32[4 * 128];
	uint8_t *buf = (uint8_t *)buf32;
	sdh_card_t c;
	sdh_bus_t b;

	SimCard f;
	f.badFrameCmd = 4;   // the first ACMD41
	b = busOf(&f);
	checkInt("R3 with end bit 0", sdh_init(&b, &c, 10000000u), SDE_FRAME);

	SimCard r6;
	r6.r6Error = true;
	b = busOf(&r6);
	checkInt("R6 reporting ERROR", sdh_init(&b, &c, 10000000u), SDE_STATUS);

	SimCard st;
	st.forceState = 3;
	b = busOf(&st);
	checkInt("card not in transfer state after init", sdh_init(&b, &c, 10000000u), SDE_STATUS);
	checkTrue("not ready", !c.ready);

	SimCard arm;
	b = busOf(&arm);
	sdh_init(&b, &c, 10000000u);
	arm.armFail = true;
	checkInt("receiver cannot be armed", sdh_read(&b, &c, 0, 1, buf), SDE_HW);
	uint8_t sds[64];
	checkInt("nor for the SD Status", sdh_sd_status(&b, &c, sds), SDE_HW);
	checkTrue("hardware errors leave the card ready", c.ready);

	SimCard stop;
	b = busOf(&stop);
	sdh_init(&b, &c, 10000000u);
	stop.stopSilent = true;
	checkInt("CMD12 unanswered after a read", sdh_read(&b, &c, 0, 2, buf), SDE_NO_RESPONSE);
	checkTrue("card marked not ready", !c.ready);
	sdh_init(&b, &c, 10000000u);
	checkInt("CMD12 unanswered after a write", sdh_write(&b, &c, 0, 2, buf), SDE_NO_RESPONSE);

	SimCard tok;
	b = busOf(&tok);
	sdh_init(&b, &c, 10000000u);
	tok.noToken = true;
	checkInt("no CRC status token", sdh_write(&b, &c, 0, 1, buf), SDE_FRAME);
	checkTrue("card still ready", c.ready);

	SimCard wp;
	b = busOf(&wp);
	sdh_init(&b, &c, 10000000u);
	wp.wpOnStatus = 1;
	wp.log.clear();
	checkInt("write-protect seen in the status after", sdh_write(&b, &c, 0, 1, buf), SDE_PROTECTED);
	checkInt("not retried", count(wp, 24), 1);
}

void runSdioTests() {
	testCrc7();
	testCrc16();
	testFraming();
	testResponses();
	testCsd();
	testTxPack();
	testCrcStatus();
	testInitTypes();
	testInitFailures();
	testRead();
	testReadErrors();
	testWrite();
	testWriteErrors();
	testStatusAndProbe();
	testFaultPaths();
}
