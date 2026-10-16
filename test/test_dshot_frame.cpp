/**
 * @file test_dshot_frame.cpp
 * @brief Host tests for the outbound DShot frame, and the map from travel onto it.
 *
 * The failures under test are the three that are silent on a wire: a checksum
 * that agrees with a shifted copy of the payload, a throttle map that walks
 * through the command range on its way up from idle, and a command frame with
 * its telemetry bit clear. An ESC (electronic speed controller) given command
 * 12 instead of 4% throttle saves its settings; nothing about that looks like
 * a driver fault.
 *
 * Ported from rcbench (https://github.com/subtilitas/rcbench), file
 * test/host/test_dshot_frame.c at commit 453cf334: the same nine cases, in
 * this suite's check helpers.
 *
 * SPDX-License-Identifier: MIT
 */

#include "check.h"

#include "../src/dshot/dshot.h"

/** @brief The 11-bit value of a frame. */
static uint16_t valueOf(uint16_t frame) { return (uint16_t)(frame >> 5); }
/** @brief The telemetry bit of a frame. */
static bool     telemOf(uint16_t frame) { return (frame & 0x0010u) != 0u; }
/** @brief The checksum nibble of a frame. */
static uint16_t csumOf(uint16_t frame)  { return (uint16_t)(frame & 0x0Fu); }

/** @brief Value, telemetry bit and checksum sit in that order. */
static void testFrameLayout() {
	section("DShot frame: value, telemetry bit, checksum");

	const uint16_t f = dshot_frame(1047u, false, false);
	checkInt("value comes back", valueOf(f), 1047);
	checkTrue("telemetry bit clear", !telemOf(f));

	const uint16_t g = dshot_frame(1047u, true, false);
	checkInt("value comes back with telemetry", valueOf(g), 1047);
	checkTrue("telemetry bit set", telemOf(g));
	// The telemetry bit is inside the checksummed payload, so asking for
	// telemetry changes the checksum as well as the bit.
	checkTrue("telemetry bit changes the checksum", csumOf(f) != csumOf(g));
}

/** @brief Every frame folds to zero, the way an ESC checks it. */
static void testChecksumFolds() {
	section("DShot frame: checksum folds the way an ESC unfolds it");

	// An ESC checks by folding the whole frame and expecting zero. If that
	// holds for every value, this end and that end agree.
	int bad = 0;
	for (uint16_t v = 0; v <= DSHOT_VALUE_MAX; ++v) {
		for (unsigned t = 0; t < 2u; ++t) {
			uint16_t c = dshot_frame(v, t != 0u, false);
			c ^= (uint16_t)(c >> 4);
			c ^= (uint16_t)(c >> 8);
			if ((c & 0x0Fu) != 0u) bad++;
		}
	}
	checkInt("frames that do not fold to 0", bad, 0);
}

/** @brief Bidirectional DShot complements the checksum. */
static void testInvertedChecksum() {
	section("DShot frame: bidirectional checksum is the complement");

	// The whole point: a receiver set up for one protocol rejects the other
	// rather than accepting a frame that means something else.
	int bad = 0;
	for (uint16_t v = 0; v <= DSHOT_VALUE_MAX; v = (uint16_t)(v + 37u)) {
		const uint16_t plain = dshot_frame(v, false, false);
		const uint16_t inv   = dshot_frame(v, false, true);
		if (valueOf(plain) != valueOf(inv)) bad++;
		if (csumOf(inv) != (uint16_t)(~csumOf(plain) & 0x0Fu)) bad++;
	}
	checkInt("values or checksums that disagree", bad, 0);
}

/** @brief A value above 2047 is sent as a well-formed stop. */
static void testOversizeIsStop() {
	section("DShot frame: a value that does not fit becomes a stop");

	checkInt("2048 sends 0", valueOf(dshot_frame(2048u, false, false)), 0);
	checkInt("0xFFFF sends 0", valueOf(dshot_frame(0xFFFFu, false, false)), 0);
	// And it is a well-formed stop, not a malformed anything.
	uint16_t c = dshot_frame(2048u, false, false);
	c ^= (uint16_t)(c >> 4);
	c ^= (uint16_t)(c >> 8);
	checkInt("and the stop folds to 0", c & 0x0Fu, 0);
}

/** @brief The exact word that asks for extended telemetry. */
static void testEdtEnableWord() {
	section("DShot frame: EDT enable carries the telemetry bit");

	// Pins the exact word that asks for extended telemetry: command 13 on a
	// bidirectional pin, telemetry bit set, complemented checksum. On a value
	// of 1 to 47 that bit is what marks the frame as a command for the
	// BLHeli_S family (Bluejay), which discards a command whose telemetry bit
	// is clear and zeroes its repeat counter with it. Ten frames of 0x01A4
	// therefore never reach the six repeats its command handler counts, and
	// temperature, voltage, current, stress and status stay empty for the
	// whole run. AM32 has no such gate and acts on either frame, so the
	// failure is one ESC family wide and silent on the others.
	//
	// esc_task.cpp picks the argument and is not in this suite; what is held
	// here is the word that argument produces.
	const uint16_t edt = dshot_frame((uint16_t)DSHOT_CMD_EDT_ENABLE, true, true);
	checkInt("command 13, bit set, is 0x01B5", edt, 0x01B5);
	checkInt("value is command 13", valueOf(edt), DSHOT_CMD_EDT_ENABLE);
	checkTrue("telemetry bit set", telemOf(edt));
	checkInt("checksum nibble", csumOf(edt), 0x05);

	// Clearing the bit moves the payload and the checksum nibble both.
	const uint16_t clear = dshot_frame((uint16_t)DSHOT_CMD_EDT_ENABLE, false, true);
	checkInt("command 13, bit clear, is 0x01A4", clear, 0x01A4);
	checkInt("its checksum nibble", csumOf(clear), 0x04);
	checkTrue("the two checksums differ", csumOf(edt) != csumOf(clear));
}

/** @brief One step above stop is already past the last command. */
static void testThrottleAvoidsCommands() {
	section("DShot throttle: never lands in the command range");

	// This is the property that stops a slow ramp from beeping and saving
	// settings.
	checkInt("0 of 1000 is stop", dshot_throttle(0u, 1000u), 0);
	int bad = 0;
	for (uint16_t c = 1u; c <= 1000u; ++c) {
		const uint16_t v = dshot_throttle(c, 1000u);
		if (v <= DSHOT_CMD_MAX || v > DSHOT_THROTTLE_MAX) bad++;
	}
	checkInt("steps that are not throttle", bad, 0);
}

/** @brief The map reaches 48 and 2047 and never goes backwards. */
static void testThrottleRange() {
	section("DShot throttle: reaches both ends and rises");

	checkInt("1 of 1000 is 48", dshot_throttle(1u, 1000u), DSHOT_THROTTLE_MIN);
	checkInt("1000 of 1000 is 2047", dshot_throttle(1000u, 1000u), DSHOT_THROTTLE_MAX);

	int backwards = 0;
	uint16_t last = 0u;
	for (uint16_t c = 1u; c <= 1000u; ++c) {
		const uint16_t v = dshot_throttle(c, 1000u);
		if (v < last) backwards++;
		last = v;
	}
	checkInt("steps that went backwards", backwards, 0);
	// A span of 1000 against 2000 codes: every step forward moves the ESC.
	checkInt("500 of 1000", dshot_throttle(500u, 1000u), 1046);
}

/** @brief Past the span is stop, as an unencodable value is. */
static void testThrottlePastSpan() {
	section("DShot throttle: past the span is stop");

	// dshot_frame() answers a value it cannot encode with stop; the map
	// answers a command past its span the same way, not with full throttle.
	checkTrue("999 is above 48", dshot_throttle(999u, 1000u) > DSHOT_THROTTLE_MIN);
	checkTrue("999 is below 2047", dshot_throttle(999u, 1000u) < DSHOT_THROTTLE_MAX);
	checkInt("1000 is 2047", dshot_throttle(1000u, 1000u), DSHOT_THROTTLE_MAX);
	checkInt("1001 is stop", dshot_throttle(1001u, 1000u), 0);
	checkInt("65535 is stop", dshot_throttle(UINT16_MAX, 1000u), 0);
	checkInt("2 of a span of 1 is stop", dshot_throttle(2u, 1u), 0);
}

/** @brief Spans of 0 and 1 are answered, not divided by. */
static void testThrottleDegenerateSpan() {
	section("DShot throttle: a degenerate span does not divide by zero");

	// Nothing configures a span of 0 or 1, and a driver that did would get a
	// division rather than a refusal, so both are answered explicitly.
	checkInt("0 of 0", dshot_throttle(0u, 0u), 0);
	checkInt("500 of 0", dshot_throttle(500u, 0u), 0);
	checkInt("1 of 1", dshot_throttle(1u, 1u), DSHOT_THROTTLE_MAX);
}

/** @brief Run every DShot frame suite. */
void runDshotFrameTests() {
	testFrameLayout();
	testChecksumFolds();
	testInvertedChecksum();
	testOversizeIsStop();
	testEdtEnableWord();
	testThrottleAvoidsCommands();
	testThrottleRange();
	testThrottlePastSpan();
	testThrottleDegenerateSpan();
}
