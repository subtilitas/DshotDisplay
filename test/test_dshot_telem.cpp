/**
 * @file test_dshot_telem.cpp
 * @brief Host tests for the bidirectional DShot reply: the line code, the
 *        group code, the checksum, the sampler, and the three outcomes of a
 *        poll.
 *
 * The failure this is really about is a plausible wrong number. An eRPM
 * (electrical revolutions per minute) frame decoded one bit out of phase is
 * still four quintets and still a number; if it passes the checksum it becomes
 * an RPM reading on a screen with nothing to mark it as wrong. So the tests
 * hold the checksum against every single-bit error, and hold the sampler
 * against a clock that does not match this end's.
 *
 * The first fourteen cases are ported from rcbench
 * (https://github.com/subtilitas/rcbench), file test/host/test_dshot_telem.c
 * at commit 453cf334, in this suite's check helpers. The rest are
 * DshotDisplay's: two decoder paths the pump depends on, dshot_reply_decode(),
 * and escFoldReply(), which turns its result into the link counters the UI
 * (user interface) shows.
 *
 * SPDX-License-Identifier: MIT
 */

#include "check.h"

#include "../src/dshot/dshot.h"
#include "../src/esc_task.h"

#include <string.h>

/** @brief Words in a test capture; the driver's own holds 8. */
#define CAP_WORDS 16u

/** @brief A sample stream as the receiver would push it. */
struct Capture {
	uint32_t word[CAP_WORDS]; /**< Samples, most significant bit of word 0 first. */
	size_t   n;               /**< Samples pushed so far. */
};

/** @brief Append @p count samples at @p level. */
static void push(Capture *c, bool level, unsigned count) {
	for (unsigned i = 0; i < count && c->n < CAP_WORDS * 32u; ++i, ++c->n) {
		if (level) c->word[c->n / 32u] |= 1u << (31u - (c->n % 32u));
	}
}

/**
 * @brief Lay the 21 line bits down at @p num / @p den samples per bit.
 *
 * So a test can ask what happens when the ESC's clock is a few percent from
 * this end's. The accumulator is what makes the error accumulate the way a
 * real one does rather than being spread evenly.
 */
static void lay(Capture *c, uint32_t line, unsigned num, unsigned den) {
	memset(c, 0, sizeof(*c));
	push(c, true, 8u);                       // the idle before the burst
	unsigned acc = 0u;
	for (int b = 20; b >= 0; --b) {
		acc += num;
		const unsigned n = acc / den;
		acc -= n * den;
		push(c, ((line >> b) & 1u) != 0u, n);
	}
	push(c, true, 8u);                       // idle again afterwards
}

// ------------------------------------------------------------- the codes

/** @brief Every 16-bit value survives encode and decode. */
static void testGcrRoundTrip() {
	section("DShot reply: every value survives the codes");

	int lost = 0;
	for (uint32_t v = 0; v <= 0xFFFFu; ++v) {
		uint16_t back = 0u;
		if (!dshot_gcr_decode(dshot_gcr_encode((uint16_t)v), &back) ||
		    back != (uint16_t)v)
			lost++;
	}
	checkInt("values that did not come back", lost, 0);
}

/** @brief The burst always begins with a falling edge. */
static void testBurstStartsLow() {
	section("DShot reply: the burst starts with a falling edge");

	// The line idles high and the response has to be findable. If the top bit
	// were ever 1 there would be no edge to find it by.
	int high = 0;
	for (uint32_t v = 0; v <= 0xFFFFu; v = v + 1021u) {
		if ((dshot_gcr_encode((uint16_t)v) & (1u << 20)) != 0u) high++;
	}
	checkInt("bursts that start high", high, 0);
}

/** @brief A quintet outside the table is refused. */
static void testBadQuintet() {
	section("DShot reply: a quintet outside the table is refused");

	// 0x00 is not a code word: five zeros in a row is exactly what the code
	// exists to prevent.
	uint16_t out = 0u;
	checkTrue("line 0 is refused", !dshot_gcr_decode(0u, &out));
	checkTrue("a NULL output is refused",
	          !dshot_gcr_decode(dshot_gcr_encode(0x1234u), nullptr));
}

// ----------------------------------------------------------- the reading

/** @brief An eRPM frame carries a period, and RPM follows from it. */
static void testErpmPeriod() {
	section("DShot reply: an eRPM frame decodes to its period");

	// 500 us per electrical revolution: mantissa 500, exponent 0.
	const uint16_t value = dshot_telem_value(DSHOT_TELEM_ERPM, 500u);
	dshot_telem_t t;
	checkTrue("decodes", dshot_telem_from_value(value, false, &t));
	checkInt("kind is eRPM", t.kind, DSHOT_TELEM_ERPM);
	checkInt("period in us", (long)t.period_us, 500);
	checkInt("eRPM", (long)t.erpm, 120000);
	checkInt("RPM at 7 pole pairs", (long)dshot_rpm(t.erpm, 7u), 17142);
	checkInt("RPM at 0 pole pairs", (long)dshot_rpm(t.erpm, 0u), 0);
}

/** @brief The exponent shifts the mantissa. */
static void testErpmExponent() {
	section("DShot reply: the exponent shifts the mantissa");

	// Mantissa 300, exponent 4: a period of 4800 us.
	const uint16_t payload = (uint16_t)((4u << 9) | 300u);
	dshot_telem_t t;
	checkTrue("decodes", dshot_telem_from_value(
	                         dshot_telem_value(DSHOT_TELEM_ERPM, payload), false, &t));
	checkInt("period in us", (long)t.period_us, 4800);
	checkInt("eRPM", (long)t.erpm, 12500);
}

/** @brief 0xFFF is a stopped motor, not a slow one. */
static void testErpmStopped() {
	section("DShot reply: a stopped motor reports no speed");

	dshot_telem_t t;
	checkTrue("decodes", dshot_telem_from_value(
	                         dshot_telem_value(DSHOT_TELEM_ERPM, 0x0FFFu), false, &t));
	checkInt("kind is eRPM", t.kind, DSHOT_TELEM_ERPM);
	checkInt("eRPM is 0", (long)t.erpm, 0);
}

/** @brief Extended frames are read as such only when EDT is on. */
static void testEdtNeedsTheFlag() {
	section("DShot reply: extended frames need EDT on");

	const uint16_t value = dshot_telem_value(DSHOT_TELEM_VOLTAGE, 66u);
	dshot_telem_t t;

	checkTrue("decodes with EDT on", dshot_telem_from_value(value, true, &t));
	checkInt("kind is voltage", t.kind, DSHOT_TELEM_VOLTAGE);
	checkInt("count", t.value, 66);

	// The same bits from an ESC that was never asked for extended telemetry
	// are an eRPM frame -- mantissa 66, exponent 2, a period of 264 us -- and
	// reading them as a voltage puts 16.5 V on the screen for a motor turning
	// at 227272 electrical RPM.
	checkTrue("decodes with EDT off", dshot_telem_from_value(value, false, &t));
	checkInt("kind is eRPM", t.kind, DSHOT_TELEM_ERPM);
}

/** @brief Each extended type comes back as itself. */
static void testEdtKinds() {
	section("DShot reply: each extended type comes back as itself");

	static const dshot_telem_kind_t kinds[] = {
		DSHOT_TELEM_TEMPERATURE, DSHOT_TELEM_VOLTAGE, DSHOT_TELEM_CURRENT,
		DSHOT_TELEM_DEBUG1, DSHOT_TELEM_DEBUG2, DSHOT_TELEM_STRESS,
		DSHOT_TELEM_STATUS,
	};
	int wrong = 0;
	for (unsigned i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
		dshot_telem_t t;
		const uint16_t v = dshot_telem_value(kinds[i], (uint16_t)(i + 3u));
		if (!dshot_telem_from_value(v, true, &t) || t.kind != kinds[i] ||
		    t.value != (uint8_t)(i + 3u))
			wrong++;
	}
	checkInt("types that came back as another", wrong, 0);
}

/** @brief The checksum catches every single-bit error. */
static void testSingleBitErrors() {
	section("DShot reply: the checksum catches every single-bit error");

	const uint16_t good = dshot_telem_value(DSHOT_TELEM_ERPM, 0x0345u);
	dshot_telem_t t;
	checkTrue("the good frame decodes", dshot_telem_from_value(good, false, &t));

	int accepted = 0;
	for (unsigned b = 0; b < 16u; ++b) {
		if (dshot_telem_from_value((uint16_t)(good ^ (uint16_t)(1u << b)), false, &t))
			accepted++;
	}
	checkInt("flipped frames still accepted", accepted, 0);
}

// --------------------------------------------------------- the sampler

/** @brief A capture at the nominal rate gives the bits back. */
static void testSamplerNominal() {
	section("DShot sampler: nominal rate");

	const uint32_t line = dshot_gcr_encode(dshot_telem_value(DSHOT_TELEM_ERPM, 500u));
	Capture cap;
	lay(&cap, line, 5u, 1u);

	uint32_t back = 0u;
	checkTrue("reads 21 bits", dshot_rx_bits(cap.word, CAP_WORDS, 5u, &back));
	checkTrue("the line it was given", back == line);
}

/** @brief The sampler holds against a clock 5% fast or slow. */
static void testSamplerClockError() {
	section("DShot sampler: an ESC clock 5% off");

	// Twenty-one bits of five percent is a whole bit of accumulated error,
	// which is what a sampler that divides a fixed count instead of
	// resynchronising gets wrong.
	const uint32_t line = dshot_gcr_encode(dshot_telem_value(DSHOT_TELEM_ERPM, 0x0123u));
	static const unsigned num[] = { 100u, 105u, 95u };
	int wrong = 0;
	for (unsigned i = 0; i < 3u; ++i) {
		Capture cap;
		lay(&cap, line, num[i], 20u);         // 5 samples a bit, scaled
		uint32_t back = 0u;
		if (!dshot_rx_bits(cap.word, CAP_WORDS, 5u, &back) || back != line) wrong++;
	}
	checkInt("rates that misread (of 3)", wrong, 0);
}

/** @brief No burst, or half a burst, is refused. */
static void testSamplerNoBurst() {
	section("DShot sampler: no burst in the capture");

	Capture cap;
	memset(&cap, 0, sizeof(cap));
	uint32_t back = 0u;

	push(&cap, true, CAP_WORDS * 32u);              // idle all the way
	checkTrue("an idle line is refused", !dshot_rx_bits(cap.word, CAP_WORDS, 5u, &back));

	// A burst that runs off the end of the capture is not 21 bits, and half a
	// burst decodes to a number as readily as a whole one.
	lay(&cap, dshot_gcr_encode(0x2222u), 5u, 1u);
	checkTrue("half a burst is refused", !dshot_rx_bits(cap.word, 2u, 5u, &back));
}

/** @brief NULL and nonsense arguments are refused. */
static void testSamplerArguments() {
	section("DShot sampler: NULL and nonsense arguments");

	uint32_t back = 0u;
	Capture cap;
	lay(&cap, dshot_gcr_encode(0x1111u), 5u, 1u);
	checkTrue("NULL samples", !dshot_rx_bits(nullptr, CAP_WORDS, 5u, &back));
	checkTrue("NULL line", !dshot_rx_bits(cap.word, CAP_WORDS, 5u, nullptr));
	checkTrue("zero words", !dshot_rx_bits(cap.word, 0u, 5u, &back));
	checkTrue("oversample of 1", !dshot_rx_bits(cap.word, CAP_WORDS, 1u, &back));
	checkTrue("NULL reading from a value", !dshot_telem_from_value(0u, false, nullptr));
	checkTrue("NULL reading from a line", !dshot_telem_decode(0u, false, nullptr));
}

/** @brief From the wire to a speed in one call. */
static void testWireToSpeed() {
	section("DShot reply: from the wire to a speed");

	// Mantissa 500, exponent 1: a period of 1000 us.
	const uint16_t value = dshot_telem_value(DSHOT_TELEM_ERPM, (uint16_t)((1u << 9) | 500u));
	Capture cap;
	lay(&cap, dshot_gcr_encode(value), 5u, 1u);

	uint32_t line = 0u;
	dshot_telem_t t;
	checkTrue("samples to bits", dshot_rx_bits(cap.word, CAP_WORDS, 5u, &line));
	checkTrue("bits to a reading", dshot_telem_decode(line, false, &t));
	checkInt("period in us", (long)t.period_us, 1000);
	checkInt("RPM at 6 pole pairs", (long)dshot_rpm(t.erpm, 6u), 10000);
}

// ------------------------------------------------------ DshotDisplay's own

/**
 * @brief A short period with EDT on is still a speed.
 *
 * The pump decodes with EDT on from the first enable onwards. Type nibble 0
 * with bit 8 clear is no extended type: it is an eRPM frame whose exponent is
 * 0, from a motor turning fast enough that the ESC did not normalise it.
 */
static void testEdtShortPeriod() {
	section("DShot reply: a short period with EDT on is a speed");

	dshot_telem_t t;
	checkTrue("decodes", dshot_telem_from_value(
	                         dshot_telem_value(DSHOT_TELEM_ERPM, 0x00FFu), true, &t));
	checkInt("kind is eRPM", t.kind, DSHOT_TELEM_ERPM);
	checkInt("period in us", (long)t.period_us, 255);
	checkInt("eRPM", (long)t.erpm, 60000000 / 255);
}

/**
 * @brief A capture that starts low is read from the idle after it.
 *
 * The receiver takes one idle sample before the burst, but a capture that
 * begins on the tail of something else carries a low run first. The edge that
 * starts the reply is the first fall after the line has been high.
 */
static void testSamplerLeadingLow() {
	section("DShot sampler: a low run before the idle is skipped");

	const uint32_t line = dshot_gcr_encode(dshot_telem_value(DSHOT_TELEM_ERPM, 500u));
	Capture cap;
	memset(&cap, 0, sizeof(cap));
	push(&cap, false, 6u);                    // the tail of something else
	push(&cap, true, 4u);                     // idle
	for (int b = 20; b >= 0; --b) push(&cap, ((line >> b) & 1u) != 0u, 5u);
	push(&cap, true, 8u);

	uint32_t back = 0u;
	checkTrue("reads 21 bits", dshot_rx_bits(cap.word, CAP_WORDS, 5u, &back));
	checkTrue("the line it was given", back == line);
}

// ------------------------------------------------ three outcomes of a poll

/**
 * @brief A capture is a good reply, no reply, or a damaged one.
 *
 * The link-quality figure on the main screen is bad replies over all replies.
 * An ESC that never answers must therefore read as missing replies, and a
 * burst that came back damaged must read as bad, or a quiet line and a noisy
 * one look alike.
 */
static void testReplyThreeStates() {
	section("DShot poll: good, none and bad are told apart");

	dshot_telem_t t;
	Capture cap;

	lay(&cap, dshot_gcr_encode(dshot_telem_value(DSHOT_TELEM_ERPM, 500u)), 5u, 1u);
	checkInt("a whole reply is GOOD",
	         dshot_reply_decode(cap.word, 8u, 5u, false, &t), DSHOT_REPLY_GOOD);
	checkInt("and carries its period", (long)t.period_us, 500);

	// The receiver pushes nothing until the line falls, so an ESC that never
	// answers leaves an empty capture, and one that has not finished leaves a
	// short one: one idle sample and 21 bits at 5 samples is 106, four words.
	checkInt("no words is NONE", dshot_reply_decode(cap.word, 0u, 5u, false, &t),
	         DSHOT_REPLY_NONE);
	checkInt("three words is NONE", dshot_reply_decode(cap.word, 3u, 5u, false, &t),
	         DSHOT_REPLY_NONE);
	checkInt("NULL samples is NONE", dshot_reply_decode(nullptr, 8u, 5u, false, &t),
	         DSHOT_REPLY_NONE);
	checkInt("NULL reading is NONE", dshot_reply_decode(cap.word, 8u, 5u, false, nullptr),
	         DSHOT_REPLY_NONE);

	// A burst whose quintets are not in the table: 21 low bits.
	lay(&cap, 0u, 5u, 1u);
	checkInt("a group-code failure is BAD",
	         dshot_reply_decode(cap.word, 8u, 5u, false, &t), DSHOT_REPLY_BAD);

	// A burst that decodes and fails its checksum.
	lay(&cap, dshot_gcr_encode((uint16_t)(dshot_telem_value(DSHOT_TELEM_ERPM, 500u) ^ 1u)),
	    5u, 1u);
	checkInt("a checksum failure is BAD",
	         dshot_reply_decode(cap.word, 8u, 5u, false, &t), DSHOT_REPLY_BAD);

	// Enough words and no falling edge in them: something was pushed and it
	// was not a reply.
	memset(&cap, 0, sizeof(cap));
	push(&cap, true, CAP_WORDS * 32u);
	checkInt("a capture with no burst is BAD",
	         dshot_reply_decode(cap.word, 8u, 5u, false, &t), DSHOT_REPLY_BAD);

	// EDT on decodes the extended types through the same call.
	lay(&cap, dshot_gcr_encode(dshot_telem_value(DSHOT_TELEM_VOLTAGE, 66u)), 5u, 1u);
	checkInt("an EDT frame is GOOD with EDT on",
	         dshot_reply_decode(cap.word, 8u, 5u, true, &t), DSHOT_REPLY_GOOD);
	checkInt("and is a voltage", t.kind, DSHOT_TELEM_VOLTAGE);
}

/** @brief Each outcome reaches the counter and field it belongs to. */
static void testFoldReply() {
	section("DShot poll: each outcome reaches its counter");

	EscTelemetry tel;
	memset(&tel, 0, sizeof(tel));
	dshot_telem_t t;
	memset(&t, 0, sizeof(t));

	escFoldReply(&tel, DSHOT_REPLY_NONE, &t, 14, 100);
	checkInt("NONE counts as no packet", (long)tel.noPackets, 1);
	escFoldReply(&tel, DSHOT_REPLY_BAD, &t, 14, 100);
	checkInt("BAD counts as a bad packet", (long)tel.badPackets, 1);
	checkInt("neither counts as good", (long)tel.goodPackets, 0);

	dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_ERPM, 500u), true, &t);
	escFoldReply(&tel, DSHOT_REPLY_GOOD, &t, 14, 200);
	checkInt("eRPM is stored", (long)tel.erpm, 120000);
	checkInt("RPM is eRPM / (poles / 2)", (long)tel.rpm, 120000 / 7);
	checkInt("and stamped", (long)tel.lastRpmMs, 200);
	checkInt("and counted good", (long)tel.goodPackets, 1);

	// Mantissa 0: a period of zero has no speed and is counted as damaged,
	// and it must not move the reading.
	dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_ERPM, (uint16_t)(3u << 9)),
	                       false, &t);
	escFoldReply(&tel, DSHOT_REPLY_GOOD, &t, 14, 250);
	checkInt("a zero period counts as bad", (long)tel.badPackets, 2);
	checkInt("and leaves eRPM alone", (long)tel.erpm, 120000);

	dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_ERPM, 0x0FFFu), true, &t);
	escFoldReply(&tel, DSHOT_REPLY_GOOD, &t, 14, 260);
	checkInt("a stopped motor is eRPM 0", (long)tel.erpm, 0);
	checkInt("and counted good", (long)tel.goodPackets, 2);

	dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_VOLTAGE, 66u), true, &t);
	escFoldReply(&tel, DSHOT_REPLY_GOOD, &t, 14, 300);
	checkInt("66 counts is 16.50 V (x100)", (long)(tel.volts * 100.0f + 0.5f), 1650);
	checkInt("voltage stamped", (long)tel.edtVoltsMs, 300);

	dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_CURRENT, 37u), true, &t);
	escFoldReply(&tel, DSHOT_REPLY_GOOD, &t, 14, 310);
	checkInt("37 counts is 37 A", (long)tel.amps, 37);
	checkInt("current stamped", (long)tel.edtAmpsMs, 310);

	dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_TEMPERATURE, 46u), true, &t);
	escFoldReply(&tel, DSHOT_REPLY_GOOD, &t, 14, 320);
	checkInt("46 counts is 46 C", tel.tempC, 46);
	checkInt("temperature stamped", (long)tel.edtTempMs, 320);

	dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_STRESS, 12u), true, &t);
	escFoldReply(&tel, DSHOT_REPLY_GOOD, &t, 14, 330);
	checkInt("stress", tel.stress, 12);
	checkInt("stress stamped", (long)tel.edtStressMs, 330);

	// Alert, warning and error set, maximum stress 7.
	dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_STATUS, 0xE7u), true, &t);
	escFoldReply(&tel, DSHOT_REPLY_GOOD, &t, 14, 340);
	checkInt("status byte kept whole", tel.statusRaw, 0xE7);
	checkInt("maximum stress is bits 3..0", tel.maxStress, 7);
	checkTrue("bit 5 is error", tel.error);
	checkTrue("bit 6 is warning", tel.warning);
	checkTrue("bit 7 is alert", tel.alert);
	checkInt("status stamped", (long)tel.edtStatusMs, 340);

	dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_STATUS, 0x03u), true, &t);
	escFoldReply(&tel, DSHOT_REPLY_GOOD, &t, 14, 345);
	checkTrue("flags clear again", !tel.error && !tel.warning && !tel.alert);

	uint32_t good = tel.goodPackets;
	dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_DEBUG1, 1u), true, &t);
	escFoldReply(&tel, DSHOT_REPLY_GOOD, &t, 14, 350);
	dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_DEBUG2, 2u), true, &t);
	escFoldReply(&tel, DSHOT_REPLY_GOOD, &t, 14, 350);
	checkInt("debug frames are counted good", (long)(tel.goodPackets - good), 2);

	// Poles below 2 leave RPM at eRPM rather than dividing by zero.
	dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_ERPM, 500u), false, &t);
	escFoldReply(&tel, DSHOT_REPLY_GOOD, &t, 1, 400);
	checkInt("one pole leaves RPM at eRPM", (long)tel.rpm, 120000);

	checkInt("no packets, all told", (long)tel.noPackets, 1);
	checkInt("bad packets, all told", (long)tel.badPackets, 2);
}

/** @brief Run every DShot reply suite. */
void runDshotTelemTests() {
	testGcrRoundTrip();
	testBurstStartsLow();
	testBadQuintet();
	testErpmPeriod();
	testErpmExponent();
	testErpmStopped();
	testEdtNeedsTheFlag();
	testEdtKinds();
	testSingleBitErrors();
	testSamplerNominal();
	testSamplerClockError();
	testSamplerNoBurst();
	testSamplerArguments();
	testWireToSpeed();
	testEdtShortPeriod();
	testSamplerLeadingLow();
	testReplyThreeStates();
	testFoldReply();
}
