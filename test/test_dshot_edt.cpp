/**
 * @file test_dshot_edt.cpp
 * @brief Host tests for when the DShot core asks an ESC for extended
 *        telemetry, held against a model of the ESC that has to accept it.
 *
 * The model is AM32 2.21's command path (AM32 is an open-source ESC firmware),
 * cut down to what decides whether command 13 is taken:
 *
 *   - a command acts only while the ESC is armed and its motor is stopped,
 *     and only on its sixth identical frame in a row (Src/dshot.c:157-166);
 *   - a zero-throttle frame clears the repeat count (Src/dshot.c:152-154);
 *   - the ESC arms itself after a second of zero input, counted from the
 *     first frame it hears (Src/main.c:1353-1356);
 *   - it restarts, forgetting everything, after 0.5 s without a frame while
 *     armed and 2 s while not (Src/main.c:1977-2002);
 *   - taking command 13 sends the status frame 0xE00 once, then voltage
 *     frames interleaved with speed (Src/dshot.c:271, 258-260).
 *
 * The bench side is dshot_edt_*(), driven the way rcbench's firmware drives
 * it: one frame a millisecond while armed, dshot_edt_idle() on every service
 * while not, and each reply heard before the next frame goes out. DshotDisplay
 * schedules its own enable with edtAutoAction(), which test_ui.cpp covers;
 * these cases keep the vendored core whole.
 *
 * Ported from rcbench (https://github.com/subtilitas/rcbench), file
 * test/host/test_dshot_edt.c at commit 453cf334: the same thirteen cases, in
 * this suite's check helpers.
 *
 * SPDX-License-Identifier: MIT
 */

#include "check.h"

#include "../src/dshot/dshot.h"

#include <string.h>

// ---------------------------------------------------------- the ESC model

/** @brief The part of AM32's state that decides whether command 13 is taken. */
struct Am32Model {
	bool     armed;          /**< Armed on a second of zero input. */
	bool     running;        /**< Armed and given a throttle. */
	unsigned zeroMs;         /**< Zero input while not armed. */
	unsigned silentMs;       /**< Since the last frame. */
	uint16_t lastCmd;        /**< The command being counted. */
	unsigned count;          /**< Identical commands in a row. */
	bool     edt;            /**< Extended telemetry on. */
	bool     initPending;    /**< The 0xE00 status frame still to send. */
	unsigned frames;         /**< Replies since extended telemetry came on. */
	unsigned voltageEvery;   /**< One voltage frame per this many replies. */
};

/** @brief Forget everything, as a restart does, except the reply pattern. */
static void am32Restart(Am32Model *m) {
	const unsigned every = m->voltageEvery;
	memset(m, 0, sizeof(*m));
	m->voltageEvery = every;
}

/** @brief One frame in; the reply's kind out. */
static dshot_telem_kind_t am32Frame(Am32Model *m, uint16_t value) {
	m->silentMs = 0u;
	const bool zeroInput = (value <= DSHOT_CMD_MAX);
	if (value == 0u) m->count = 0u;
	if (zeroInput) {
		m->running = false;
		if (!m->armed && ++m->zeroMs > 1000u) m->armed = true;
	} else {
		m->zeroMs = 0u;
		if (m->armed) m->running = true;
	}
	if (value >= 1u && value <= DSHOT_CMD_MAX && m->armed && !m->running) {
		if (value != m->lastCmd) {
			m->lastCmd = value;
			m->count = 0u;
		}
		if (++m->count >= 6u) {
			m->count = 0u;
			if (value == DSHOT_CMD_EDT_ENABLE) {
				m->edt = true;
				m->initPending = true;
			}
		}
	}

	if (m->initPending) {
		m->initPending = false;
		return DSHOT_TELEM_STATUS;
	}
	if (m->edt && m->voltageEvery > 0u && ++m->frames % m->voltageEvery == 0u)
		return DSHOT_TELEM_VOLTAGE;
	return DSHOT_TELEM_ERPM;
}

/** @brief A millisecond with nothing on the wire. */
static void am32Silence(Am32Model *m) {
	++m->silentMs;
	if ((m->armed && m->silentMs > 500u) || m->silentMs > 2000u) am32Restart(m);
}

// --------------------------------------------------------------- the bench

/** @brief The sending end, and what it did. */
struct Bench {
	dshot_edt_t edt;            /**< The schedule under test. */
	Am32Model   esc;            /**< What it is talking to. */
	uint32_t    nowMs;          /**< Virtual time. */
	unsigned    commands;       /**< Command 13 frames sent. */
	unsigned    lateCommands;   /**< Sent after the answer was heard. */
	unsigned    heldThrottle;   /**< Throttle asked for, command sent. */
	uint32_t    answeredMs;     /**< When the bench heard the answer. */
	unsigned    startsAfterOn;  /**< Asks begun after the ESC took one. */
	uint32_t    askStart[64];   /**< When each ask began. */
	unsigned    nStarts;        /**< Entries in askStart. */
	bool        inAsk;          /**< An ask is under way. */
};

/** @brief A new binding at @p nowMs, against an ESC with voltage every 50. */
static void benchInit(Bench *b, uint32_t nowMs) {
	memset(b, 0, sizeof(*b));
	b->nowMs = nowMs;
	b->esc.voltageEvery = 50u;
	dshot_edt_bind(&b->edt, nowMs);
}

/** @brief Disarmed for @p ms: nothing sent, the ask owed again. */
static void benchIdle(Bench *b, unsigned ms) {
	for (unsigned i = 0; i < ms; ++i) {
		dshot_edt_idle(&b->edt, b->nowMs);
		am32Silence(&b->esc);
		b->inAsk = false;
		++b->nowMs;
	}
}

/**
 * @brief Armed for @p ms at @p throttle (a DShot value, 0 or 48..2047).
 *
 * The reply is read as the decoder would: as a speed unless an ask has
 * started.
 */
static void benchDrive(Bench *b, unsigned ms, uint16_t throttle) {
	for (unsigned i = 0; i < ms; ++i) {
		const bool wasAnswered = b->edt.answered;
		const bool ask = dshot_edt_frame(&b->edt, throttle == 0u, b->nowMs);
		if (ask) {
			++b->commands;
			if (wasAnswered) ++b->lateCommands;
			if (throttle != 0u) ++b->heldThrottle;
			if (!b->inAsk && b->nStarts < 64u) b->askStart[b->nStarts++] = b->nowMs;
			if (!b->inAsk && b->esc.edt) ++b->startsAfterOn;
		}
		b->inAsk = ask && b->edt.left != 0u;
		dshot_telem_kind_t kind =
			am32Frame(&b->esc, ask ? (uint16_t)DSHOT_CMD_EDT_ENABLE : throttle);
		if (!b->edt.asked) kind = DSHOT_TELEM_ERPM;
		dshot_edt_heard(&b->edt, kind);
		if (!wasAnswered && b->edt.answered) b->answeredMs = b->nowMs;
		++b->nowMs;
	}
}

// ------------------------------------------------------------------ cases

/** @brief Ten frames on the edge into a run arrive before AM32 has armed. */
static void testOneAskIsTooEarly() {
	section("DShot EDT: one ask at the start of a run never reaches AM32");

	// DSHOT_CMD_REPEATS frames of command 13 on the edge into driving, then
	// throttle, from a line that was silent. AM32 has not armed 10 ms after the
	// line came alive, so it drops them, and nothing asks again.
	Am32Model esc;
	memset(&esc, 0, sizeof(esc));
	esc.voltageEvery = 50u;
	for (unsigned ms = 0; ms < 5000u; ++ms) {
		const uint16_t v = (ms < DSHOT_CMD_REPEATS) ? (uint16_t)DSHOT_CMD_EDT_ENABLE : 0u;
		(void)am32Frame(&esc, v);
	}
	checkTrue("the ESC armed", esc.armed);
	checkTrue("and never took the ask", !esc.edt);
}

/** @brief The repeated ask lands once AM32 has armed. */
static void testRepeatedAskLands() {
	section("DShot EDT: AM32 takes the repeated ask once armed");

	Bench b;
	benchInit(&b, 1000u);
	benchIdle(&b, 3000u);                    // the ESC restarts in here
	benchDrive(&b, 5000u, 0u);

	checkTrue("the ESC took it", b.esc.edt);
	checkTrue("the bench heard it", b.edt.answered);
	// AM32 arms on its 1001st millisecond of zero input; the ask that is due
	// after that is the one it takes.
	checkTrue("within one retry of arming",
	          b.answeredMs - 4000u <= 1001u + DSHOT_EDT_RETRY_MS + DSHOT_CMD_REPEATS);
	checkInt("no command after the answer", b.lateCommands, 0);
	checkInt("no ask begun after the ESC took one", b.startsAfterOn, 0);
}

/** @brief The 0xE00 status frame alone counts as the answer. */
static void testStatusFrameIsAnswer() {
	section("DShot EDT: the status frame alone is the answer");

	// No voltage frames at all: the 0xE00 AM32 sends once when it takes the
	// command is the only extended frame, and it must not be read as a speed.
	Bench b;
	benchInit(&b, 0u);
	b.esc.voltageEvery = 0u;
	benchDrive(&b, 3000u, 0u);
	checkTrue("the ESC took it", b.esc.edt);
	checkTrue("the bench heard it", b.edt.answered);
	checkInt("no command after the answer", b.lateCommands, 0);
	// Heard on the ask AM32 took, not on a later one: a status frame read as a
	// speed is lost, and only the next ask would bring another.
	checkInt("no ask begun after the ESC took one", b.startsAfterOn, 0);
}

/** @brief An ESC already armed takes the first ask on its sixth repeat. */
static void testFirstAskHeard() {
	section("DShot EDT: an ESC that takes the first ask is heard on it");

	// An ESC already armed when the run starts -- Bluejay, which takes
	// commands without arming, or an AM32 left for less than its 0.5 s
	// restart -- takes the first ask on its sixth repeat. Its status frame
	// arrives before that ask has finished.
	Bench b;
	benchInit(&b, 0u);
	b.esc.voltageEvery = 0u;
	b.esc.armed = true;
	benchDrive(&b, 2000u, 0u);
	checkTrue("the ESC took it", b.esc.edt);
	checkTrue("the bench heard it", b.edt.answered);
	checkInt("six command frames went out", b.commands, 6);
	checkInt("no ask begun after the ESC took one", b.startsAfterOn, 0);
}

/** @brief The first frame of a run at zero throttle is the ask. */
static void testFirstFrameAsks() {
	section("DShot EDT: the first frame at zero throttle is the ask");

	dshot_edt_t e;
	dshot_edt_bind(&e, 0u);
	int sent = 0;
	for (unsigned i = 0; i < DSHOT_CMD_REPEATS; ++i) {
		if (dshot_edt_frame(&e, true, i)) sent++;
	}
	checkInt("the first ten frames ask", sent, DSHOT_CMD_REPEATS);
	checkTrue("the eleventh does not", !dshot_edt_frame(&e, true, DSHOT_CMD_REPEATS));
	checkTrue("asked is set", e.asked);
	checkInt("one ask completed", e.asks, 1);
}

/** @brief A throttle always goes out; the ask it interrupts starts over. */
static void testThrottleWins() {
	section("DShot EDT: a throttle is never held back by an ask");

	Bench b;
	benchInit(&b, 0u);
	benchDrive(&b, 4u, 0u);                  // four repeats of the first ask
	benchDrive(&b, 2000u, 600u);             // the operator opens the throttle
	checkInt("no throttle replaced by a command", b.heldThrottle, 0);
	checkInt("four command frames went out", b.commands, 4);

	// Back at zero, the interrupted ask starts again from its first repeat: the
	// four that went out were followed by throttle, which an ESC does not count
	// as part of a run of commands.
	dshot_edt_t *e = &b.edt;
	checkInt("no ask completed", e->asks, 0);
	checkTrue("the next zero frame asks", dshot_edt_frame(e, true, b.nowMs));
	checkInt("from the first repeat", e->left, DSHOT_CMD_REPEATS - 1u);
}

/** @brief A run that starts above zero asks once the throttle closes. */
static void testOpenThrottleRun() {
	section("DShot EDT: a run armed with the throttle open asks once it closes");

	// The arming frame carries a throttle, so a run can start above zero.
	// AM32 does not arm on that, and the bench does not ask into it.
	Bench b;
	benchInit(&b, 0u);
	benchDrive(&b, 2000u, 400u);
	checkInt("no command with the throttle open", b.commands, 0);
	checkTrue("the ESC has not armed", !b.esc.armed);
	benchDrive(&b, 3000u, 0u);
	checkTrue("the ESC took it", b.esc.edt);
	checkTrue("the bench heard it", b.edt.answered);
}

/** @brief An ESC that never answers is asked ten times, 509 ms apart. */
static void testBoundedAsks() {
	section("DShot EDT: an ESC that never answers is asked a bounded number of times");

	// An ESC that ignores command 13 answers every frame with a speed.
	Bench b;
	benchInit(&b, 0u);
	for (unsigned ms = 0; ms < 60000u; ++ms) {
		const bool ask = dshot_edt_frame(&b.edt, true, b.nowMs);
		if (ask) {
			++b.commands;
			if (!b.inAsk && b.nStarts < 64u) b.askStart[b.nStarts++] = b.nowMs;
		}
		b.inAsk = ask && b.edt.left != 0u;
		dshot_edt_heard(&b.edt, DSHOT_TELEM_ERPM);
		++b.nowMs;
	}
	checkInt("command frames in all", b.commands, DSHOT_EDT_ASKS * DSHOT_CMD_REPEATS);
	checkInt("asks begun", b.nStarts, DSHOT_EDT_ASKS);
	int offBeat = 0;
	for (unsigned i = 1; i < b.nStarts; ++i) {
		// Due DSHOT_EDT_RETRY_MS after the last repeat of the ask before.
		if (b.askStart[i] - b.askStart[i - 1] != DSHOT_EDT_RETRY_MS + DSHOT_CMD_REPEATS - 1u)
			offBeat++;
	}
	checkInt("asks not 509 ms apart", offBeat, 0);
	checkTrue("never answered", !b.edt.answered);
}

/** @brief Every run asks again; the binding keeps reading extended frames. */
static void testEveryRunAsks() {
	section("DShot EDT: every run asks again");

	Bench b;
	benchInit(&b, 0u);
	benchDrive(&b, 3000u, 0u);
	checkTrue("the first run is answered", b.edt.answered);

	// Disarmed long enough for AM32 to restart and forget.
	benchIdle(&b, 1000u);
	checkTrue("the ESC forgot", !b.esc.edt);
	checkTrue("the answer is owed again", !b.edt.answered);
	checkTrue("replies still read extended", b.edt.asked);

	const unsigned before = b.commands;
	benchDrive(&b, 3000u, 0u);
	checkTrue("the second run asked", b.commands > before);
	checkTrue("the ESC took it", b.esc.edt);
	checkTrue("the bench heard it", b.edt.answered);

	// A new binding is a new ESC as far as anyone knows.
	dshot_edt_bind(&b.edt, b.nowMs);
	checkTrue("a new binding has not asked", !b.edt.asked);
	checkTrue("and has heard nothing", !b.edt.answered);
}

/** @brief The retry clock survives the millisecond counter wrapping. */
static void testRetryWrap() {
	section("DShot EDT: the retry clock survives the millisecond wrap");

	Bench b;
	benchInit(&b, 0xFFFFFFFFu - 700u);
	int asksSeen = 0;
	for (unsigned ms = 0; ms < 3000u; ++ms) {
		if (dshot_edt_frame(&b.edt, true, b.nowMs) && b.edt.left == 0u) ++asksSeen;
		++b.nowMs;
	}
	// Asks start at 0, 509, 1018, 1527, 2036 and 2545 ms after the bind; the
	// wrap falls between the second and the third.
	checkInt("asks completed in 3 s", asksSeen, 6);
}

/** @brief Every entry point refuses a NULL state. */
static void testNullState() {
	section("DShot EDT: a NULL state is refused everywhere");

	dshot_edt_bind(nullptr, 0u);
	dshot_edt_idle(nullptr, 0u);
	dshot_edt_heard(nullptr, DSHOT_TELEM_STATUS);
	checkTrue("no ask from a NULL state", !dshot_edt_frame(nullptr, true, 0u));
}

/** @brief An extended kind before any ask is not an answer. */
static void testHeardBeforeAsk() {
	section("DShot EDT: an extended kind before any ask is not an answer");

	dshot_edt_t e;
	dshot_edt_bind(&e, 0u);
	dshot_edt_heard(&e, DSHOT_TELEM_VOLTAGE);
	checkTrue("not answered", !e.answered);
	checkTrue("the ask still goes out", dshot_edt_frame(&e, true, 0u));
}

/** @brief The extended units are the documented ones. */
static void testEdtUnits() {
	section("DShot EDT: the units are the documented ones");

	checkInt("0 counts is 0 mV", (long)dshot_edt_millivolts(0u), 0);
	checkInt("1 count is 250 mV", (long)dshot_edt_millivolts(1u), 250);
	checkInt("48 counts is 12.00 V", (long)dshot_edt_millivolts(48u), 12000);
	checkInt("255 counts is 63.75 V", (long)dshot_edt_millivolts(255u), 63750);
	checkInt("37 counts is 37 A", (long)dshot_edt_amps(37u), 37);
	checkInt("255 counts is 255 A", (long)dshot_edt_amps(255u), 255);
	checkInt("25 counts is 25 C", (long)dshot_edt_celsius(25u), 25);
}

/** @brief Run every DShot EDT suite. */
void runDshotEdtTests() {
	testOneAskIsTooEarly();
	testRepeatedAskLands();
	testStatusFrameIsAnswer();
	testFirstAskHeard();
	testFirstFrameAsks();
	testThrottleWins();
	testOpenThrottleRun();
	testBoundedAsks();
	testEveryRunAsks();
	testRetryWrap();
	testNullState();
	testHeardBeforeAsk();
	testEdtUnits();
}
