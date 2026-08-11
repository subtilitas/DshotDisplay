/**
 * @file test_blackbox.cpp
 * @brief Host tests for the blackbox encoder.
 *
 * The primitive tests here pin down the encodings. The test that actually
 * matters is not in this file: `make bbcheck` generates a log and runs the real
 * `blackbox_decode` over it. Hand-written byte expectations only prove the
 * encoder agrees with my reading of the spec; the decoder proves it agrees with
 * the tool people will use.
 */

#include "check.h"

#include "../src/blackbox_encode.h"

#include <stdio.h>
#include <string.h>

/** @brief Sink that appends to a fixed buffer, for inspecting output. */
struct BufSink {
	uint8_t buf[8192];
	size_t  len;
};

static void bufWrite(void *ctx, const uint8_t *data, size_t len) {
	BufSink *b = (BufSink *)ctx;
	if (b->len + len > sizeof(b->buf)) return;
	memcpy(b->buf + b->len, data, len);
	b->len += len;
}

/** @brief Reference VB decoder, so round-trips are checked against a reader. */
static uint32_t readUnsignedVB(const uint8_t *p, uint8_t *consumed) {
	uint32_t result = 0;
	int shift = 0;
	for (int i = 0; i < 5; i++) {
		uint8_t c = p[i];
		result |= (uint32_t)(c & 0x7F) << shift;
		if (c < 128) { *consumed = (uint8_t)(i + 1); return result; }
		shift += 7;
	}
	*consumed = 5;
	return result;
}

/** @brief Zig-zag decode, matching the decoder's zigzagDecode(). */
static int32_t unZigZag(uint32_t v) {
	return (int32_t)(v >> 1) ^ -(int32_t)(v & 1);
}

/** @brief Variable-byte encoding, at the byte-count boundaries. */
static void testUnsignedVB() {
	section("Unsigned variable-byte");

	uint8_t b[5];
	checkInt("0 is one byte",       bbWriteUnsignedVB(0, b), 1);
	checkInt("  and is 0x00",       b[0], 0x00);
	checkInt("127 is one byte",     bbWriteUnsignedVB(127, b), 1);
	checkInt("128 is two bytes",    bbWriteUnsignedVB(128, b), 2);
	checkInt("16383 is two bytes",  bbWriteUnsignedVB(16383, b), 2);
	checkInt("16384 is three",      bbWriteUnsignedVB(16384, b), 3);
	checkInt("UINT32_MAX is five",  bbWriteUnsignedVB(0xFFFFFFFFu, b), 5);

	// Round-trip through an independent reader across the boundaries and a
	// spread of larger values.
	const uint32_t vals[] = {0, 1, 127, 128, 129, 16383, 16384, 2097151,
	                         2097152, 268435455, 268435456, 0xFFFFFFFFu};
	bool ok = true;
	for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		uint8_t n = bbWriteUnsignedVB(vals[i], b), used = 0;
		if (readUnsignedVB(b, &used) != vals[i] || used != n) ok = false;
	}
	checkTrue("round-trips at every boundary", ok);
}

/** @brief Zig-zag, which is what keeps small negatives cheap. */
static void testZigZag() {
	section("Zig-zag");

	checkInt("0 -> 0",   bbZigZag(0), 0);
	checkInt("-1 -> 1",  bbZigZag(-1), 1);
	checkInt("1 -> 2",   bbZigZag(1), 2);
	checkInt("-2 -> 3",  bbZigZag(-2), 3);
	checkInt("2 -> 4",   bbZigZag(2), 4);

	// The point of the whole exercise: without zig-zag, -1 is 0xFFFFFFFF and
	// costs five bytes. A field that ticks down by one per frame would be the
	// most expensive thing in the log.
	uint8_t b[5];
	checkInt("-1 costs one byte", bbWriteSignedVB(-1, b), 1);
	checkInt("-64 costs one byte", bbWriteSignedVB(-64, b), 1);

	bool ok = true;
	const int32_t vals[] = {0, 1, -1, 63, -64, 64, -65, 8191, -8192,
	                        2147483647, -2147483647 - 1};
	for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		uint8_t n = bbWriteSignedVB(vals[i], b), used = 0;
		if (unZigZag(readUnsignedVB(b, &used)) != vals[i] || used != n) ok = false;
	}
	checkTrue("signed round-trips, INT32_MIN included", ok);
}

/** @brief Header shape: the parts the decoder searches for byte-exactly. */
static void testHeader() {
	section("Header");

	BufSink b; b.len = 0;
	BlackboxSink sink = { bufWrite, &b };
	BlackboxEncoder e;
	bbBegin(&e, &sink, 32);

	b.buf[b.len] = 0;
	const char *s = (const char *)b.buf;

	// The parser memmem()s for this exact string to locate a log. One byte out
	// and the file contains no logs at all.
	checkTrue("opens with the product marker",
	          strncmp(s, "H Product:Blackbox flight data recorder by "
	                     "Nicholas Sherlock\n", 60) == 0);
	checkTrue("declares data version 2", strstr(s, "H Data version:2\n") != NULL);
	checkTrue("I interval echoed", strstr(s, "H I interval:32\n") != NULL);
	checkTrue("P interval present", strstr(s, "H P interval:1/1\n") != NULL);

	// Field 0 and 1 are fixed by the decoder, which hardcodes those indices.
	checkTrue("field 0 is loopIteration",
	          strstr(s, "H Field I name:loopIteration,time,") != NULL);

	// Every per-field line must list exactly BB_FIELD_COUNT values, or the
	// decoder silently reads a different field count than we write.
	const char *lines[] = {"H Field I signed:", "H Field I predictor:",
	                       "H Field I encoding:", "H Field P predictor:",
	                       "H Field P encoding:"};
	bool ok = true;
	for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
		const char *p = strstr(s, lines[i]);
		if (!p) { ok = false; break; }
		p += strlen(lines[i]);
		int commas = 0;
		for (; *p && *p != '\n'; p++) if (*p == ',') commas++;
		if (commas != BB_FIELD_COUNT - 1) ok = false;
	}
	checkTrue("every field line has BB_FIELD_COUNT entries", ok);
}

/** @brief Frame selection and the cost of a steady signal. */
static void testFrames() {
	section("Frames");

	BufSink b; b.len = 0;
	BlackboxSink sink = { bufWrite, &b };
	BlackboxEncoder e;
	bbBegin(&e, &sink, 4);
	size_t headerLen = b.len;

	int32_t v[BB_FIELD_COUNT] = {0};
	v[BB_F_THROTTLE] = 500;
	v[BB_F_ERPM] = 70000;

	bbWriteFrame(&e, 1000, v);
	checkInt("first frame is an I frame", e.iFrames, 1);
	checkInt("  and no P frames yet", e.pFrames, 0);
	checkTrue("marked 'I' on the wire", b.buf[headerLen] == 'I');

	size_t afterI = b.len;
	bbWriteFrame(&e, 2000, v);
	checkInt("second frame is a P frame", e.pFrames, 1);
	checkTrue("marked 'P' on the wire", b.buf[afterI] == 'P');

	// The first P frame after an I frame cannot be minimal: there is only one
	// frame of history, so the straight-line time predictor has nothing to
	// extrapolate from and falls back to "same as previous". The residual is
	// then the whole frame interval rather than zero.
	size_t firstP = b.len - afterI;
	checkInt("first P frame carries a time residual", (long)firstP, 13);

	// From the third frame on, two frames of history exist and a steady signal
	// on a fixed cadence costs the floor: one marker byte, one zero byte per
	// non-null field, and nothing at all for loopIteration.
	size_t before = b.len;
	bbWriteFrame(&e, 3000, v);
	checkInt("steady P frame is at the floor", (long)(b.len - before),
	         1 + (BB_FIELD_COUNT - 1));

	// I frames land on iteration % iInterval == 0, which is what the decoder's
	// shouldHaveFrame() computes. With iInterval 4 that is iterations 0 and 4,
	// so the fifth frame is the next I frame -- not the fourth.
	bbWriteFrame(&e, 4000, v);
	checkInt("still one I frame after four", e.iFrames, 1);
	bbWriteFrame(&e, 5000, v);
	checkInt("I frame returns on the fifth", e.iFrames, 2);

	bbEnd(&e);
	checkTrue("ends with the log-end sentinel",
	          memcmp(b.buf + b.len - 11, "End of log", 10) == 0);
	checkInt("  and a trailing NUL", b.buf[b.len - 1], 0);
}

/** @brief Straight-line time prediction, the one non-trivial predictor. */
static void testTimePrediction() {
	section("Time prediction");

	BufSink b; b.len = 0;
	BlackboxSink sink = { bufWrite, &b };
	BlackboxEncoder e;
	bbBegin(&e, &sink, 1000);

	int32_t v[BB_FIELD_COUNT] = {0};

	// Perfectly regular 2 ms spacing. Once two frames of history exist, the
	// straight-line predictor should be exact and the residual zero.
	bbWriteFrame(&e, 10000, v);
	bbWriteFrame(&e, 12000, v);
	size_t before = b.len;
	bbWriteFrame(&e, 14000, v);
	size_t third = b.len - before;

	before = b.len;
	bbWriteFrame(&e, 16000, v);
	size_t fourth = b.len - before;

	checkInt("regular cadence costs the same each frame", (long)fourth, (long)third);
	// One marker byte plus one zero byte per non-null field is the floor.
	checkTrue("and is at the minimum size", fourth <= 12);

	// A jump in cadence must still encode, just less cheaply.
	before = b.len;
	bbWriteFrame(&e, 99000, v);
	checkTrue("a timing jump costs more", (b.len - before) > fourth);
}

/** @brief Run every blackbox suite. */
void runBlackboxTests() {
	testUnsignedVB();
	testZigZag();
	testHeader();
	testFrames();
	testTimePrediction();
}
