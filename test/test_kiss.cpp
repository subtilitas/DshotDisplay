/**
 * @file test_kiss.cpp
 * @brief Host tests for the KISS telemetry decoder.
 *
 * The decoder is pure, so everything here runs without hardware or stubs.
 */

#include "check.h"

#include "../src/kiss_telem.h"
#include "../src/esc_merge.h"
#include "../src/esc_task.h"

#include <string.h>

/**
 * @brief Build a valid frame from physical values, CRC included.
 *
 * Independent of the decoder — it assembles bytes by hand rather than calling
 * anything in kiss_telem.cpp — so a decoder that got the byte order wrong could
 * not make its own tests pass.
 */
static void makeFrame(uint8_t *out, uint8_t tempC, float volts, float amps,
                      uint16_t mah, uint32_t erpm) {
	uint16_t v = (uint16_t)(volts * 100.0f + 0.5f);
	uint16_t a = (uint16_t)(amps * 100.0f + 0.5f);
	uint16_t r = (uint16_t)(erpm / 100u);

	out[0] = tempC;
	out[1] = (uint8_t)(v >> 8);   out[2] = (uint8_t)(v & 0xFF);
	out[3] = (uint8_t)(a >> 8);   out[4] = (uint8_t)(a & 0xFF);
	out[5] = (uint8_t)(mah >> 8); out[6] = (uint8_t)(mah & 0xFF);
	out[7] = (uint8_t)(r >> 8);   out[8] = (uint8_t)(r & 0xFF);
	out[9] = kissCrc8(out, 9);
}

/** @brief CRC8, checked against a published value rather than against itself. */
static void testCrc() {
	section("KISS CRC8 is plain CRC-8, poly 0x07");

	// The CRC catalogue's check value: CRC-8/SMBUS over the ASCII digits
	// "123456789" is 0xF4. This pins down polynomial, init, reflection and
	// final XOR all at once, and it comes from outside this codebase — a
	// self-consistent implementation of the *wrong* CRC cannot pass it.
	const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
	checkInt("catalogue check value", kissCrc8(check, 9), 0xF4);

	const uint8_t zeros[9] = {0};
	checkInt("all-zero payload", kissCrc8(zeros, 9), 0x00);

	// Single-byte vectors, hand-checkable: 0x01 through the poly once.
	const uint8_t one[1] = {0x01};
	checkInt("single 0x01 byte", kissCrc8(one, 1), 0x07);
}

/** @brief A realistic frame decodes to the right physical values. */
static void testDecode() {
	section("Decoding a frame");

	uint8_t f[KISS_FRAME_LEN];
	makeFrame(f, 32, 16.80f, 12.34f, 1234, 70000);

	KissFrame k;
	checkTrue("valid frame accepted", kissDecodeFrame(f, &k));
	checkInt("temperature",        k.tempC, 32);
	checkInt("volts x100",         (long)(k.volts * 100.0f + 0.5f), 1680);
	checkInt("amps x100",          (long)(k.amps  * 100.0f + 0.5f), 1234);
	checkInt("consumption mAh",    k.mah, 1234);
	checkInt("eRPM",               k.erpm, 70000);

	// Voltage 16.80 V is 0x0690. Byte-swapped it would read 0x9006 = 368.70 V,
	// so this value fails loudly if the endianness is ever flipped. The rest of
	// DShot is not big-endian, which makes that a live risk.
	checkTrue("volts not byte-swapped", k.volts > 16.0f && k.volts < 17.0f);
}

/** @brief Range ends, where the u16 fields saturate. */
static void testRanges() {
	section("Field ranges");

	uint8_t f[KISS_FRAME_LEN];
	KissFrame k;

	makeFrame(f, 0, 0.0f, 0.0f, 0, 0);
	checkTrue("all-zero frame is valid", kissDecodeFrame(f, &k));
	checkInt("zero eRPM", k.erpm, 0);

	// 0xFFFF in every field: 655.35 V, 655.35 A, 65535 mAh, 6,553,500 eRPM.
	// eRPM is the one that matters — x100 overflows a u16, so the decoder has
	// to widen before multiplying.
	memset(f, 0xFF, 9);
	f[9] = kissCrc8(f, 9);
	checkTrue("all-ones frame is valid", kissDecodeFrame(f, &k));
	checkInt("max eRPM does not overflow", k.erpm, 6553500);
	checkInt("max mAh", k.mah, 65535);
	checkInt("max volts x100", (long)(k.volts * 100.0f + 0.5f), 65535);
}

/** @brief Every single-bit corruption is caught. */
static void testCrcRejection() {
	section("Corrupted frames are rejected");

	uint8_t good[KISS_FRAME_LEN];
	makeFrame(good, 25, 12.00f, 3.50f, 500, 30000);

	KissFrame k;
	int accepted = 0;
	for (int bit = 0; bit < KISS_FRAME_LEN * 8; bit++) {
		uint8_t f[KISS_FRAME_LEN];
		memcpy(f, good, sizeof(f));
		f[bit / 8] ^= (uint8_t)(1u << (bit % 8));
		if (kissDecodeFrame(f, &k)) accepted++;
	}
	// CRC-8 detects every single-bit error in a message this short, so the
	// count must be exactly zero, not merely small.
	checkInt("single-bit flips accepted (of 80)", accepted, 0);
}

/** @brief Byte-at-a-time assembly, which is how the UART will feed it. */
static void testStreaming() {
	section("Streaming bytes in");

	uint8_t f[KISS_FRAME_LEN];
	makeFrame(f, 40, 22.20f, 1.00f, 10, 12300);

	KissDecoder d;
	memset(&d, 0, sizeof(d));
	KissFrame k;

	// Nine bytes must not complete a frame.
	int completions = 0;
	for (int i = 0; i < KISS_FRAME_LEN - 1; i++)
		if (kissFeed(&d, f[i], &k)) completions++;
	checkInt("no completion before byte 10", completions, 0);

	checkTrue("byte 10 completes it", kissFeed(&d, f[9], &k));
	checkInt("streamed eRPM", k.erpm, 12300);
	checkInt("good count", d.good, 1);
	checkInt("bad count", d.bad, 0);

	// A second frame straight after the first, with no reset in between.
	makeFrame(f, 41, 22.10f, 2.00f, 11, 12400);
	completions = 0;
	for (int i = 0; i < KISS_FRAME_LEN; i++)
		if (kissFeed(&d, f[i], &k)) completions++;
	checkInt("back-to-back frame decodes", completions, 1);
	checkInt("good count after two", d.good, 2);
	checkInt("second frame eRPM", k.erpm, 12400);
}

/** @brief Bad frames clear the buffer instead of poisoning the next one. */
static void testStreamingRecovery() {
	section("Recovering from a bad frame");

	uint8_t f[KISS_FRAME_LEN];
	makeFrame(f, 30, 15.00f, 5.00f, 100, 25000);
	f[4] ^= 0x20;                       // corrupt the current field

	KissDecoder d;
	memset(&d, 0, sizeof(d));
	KissFrame k;

	int completions = 0;
	for (int i = 0; i < KISS_FRAME_LEN; i++)
		if (kissFeed(&d, f[i], &k)) completions++;
	checkInt("corrupt frame not reported", completions, 0);
	checkInt("counted as bad", d.bad, 1);
	checkInt("buffer emptied", d.have, 0);

	// The very next frame must decode, which is the thing that breaks if a
	// failed frame is left in the buffer to be retried a byte at a time.
	makeFrame(f, 31, 15.10f, 5.10f, 101, 25100);
	completions = 0;
	for (int i = 0; i < KISS_FRAME_LEN; i++)
		if (kissFeed(&d, f[i], &k)) completions++;
	checkInt("next frame decodes cleanly", completions, 1);
	checkInt("eRPM after recovery", k.erpm, 25100);
}

/** @brief Resynchronisation, which is where a delimiter-free protocol hurts. */
static void testResync() {
	section("Resynchronising after junk");

	KissDecoder d;
	memset(&d, 0, sizeof(d));
	KissFrame k;

	// Four bytes of the ESC's power-on chatter, then a request goes out. Without
	// the reset those four would shift the real frame out of alignment.
	const uint8_t junk[] = {0xAA, 0x55, 0x13, 0x37};
	for (unsigned i = 0; i < sizeof(junk); i++) kissFeed(&d, junk[i], &k);
	checkInt("junk is buffered", d.have, 4);

	kissExpectFrame(&d);
	checkInt("expectFrame drops the partial", d.have, 0);

	uint8_t f[KISS_FRAME_LEN];
	makeFrame(f, 20, 11.10f, 0.50f, 5, 5000);
	int completions = 0;
	for (int i = 0; i < KISS_FRAME_LEN; i++)
		if (kissFeed(&d, f[i], &k)) completions++;
	checkInt("aligned frame decodes", completions, 1);
	checkInt("eRPM after resync", k.erpm, 5000);

	// Trailing bytes with no request outstanding are counted, not shifted in.
	kissFeed(&d, 0x00, &k);
	kissFeed(&d, 0x00, &k);
	checkInt("stray bytes buffered, not dropped", d.have, 2);
	checkInt("no phantom frame", d.good, 1);
}

/**
 * @brief The 12-bit DShot payload builder.
 *
 * The library's sendThrottle() can never set the telemetry-request bit, so this
 * replaces it. Getting the zero case wrong turns motor-stop into a live command,
 * which is the reason these checks exist.
 */
static void testPayloadBuilder() {
	section("DShot payload with the telemetry bit");

	// Zero must stay zero. sendThrottle() skips the +47 offset for zero so that
	// it remains the motor-stop command; adding 47 here would command 47/2047.
	checkInt("throttle 0, no telem", kissBuildDshotPayload(0, false), 0);
	checkInt("throttle 0, telem",    kissBuildDshotPayload(0, true),  1);

	// Non-zero throttle carries the 47-command offset.
	checkInt("throttle 1 -> (1+47)<<1", kissBuildDshotPayload(1, false), 96);
	checkInt("throttle 1, telem set",   kissBuildDshotPayload(1, true),  97);

	checkInt("throttle 953 -> (953+47)<<1", kissBuildDshotPayload(953, false), 2000);

	// Clamped like the library does, and the clamp happens before the offset.
	checkInt("throttle 2000 clamped", kissBuildDshotPayload(2000, false), (2000 + 47) << 1);
	checkInt("throttle 9999 clamped", kissBuildDshotPayload(9999, false), (2000 + 47) << 1);

	// The request bit is bit 0 and must not disturb the throttle bits.
	for (uint16_t t = 0; t <= 2000; t += 137) {
		uint16_t off = kissBuildDshotPayload(t, false);
		uint16_t on  = kissBuildDshotPayload(t, true);
		if ((off | 1u) != on || (off & 1u) != 0u) {
			checkTrue("telem bit is bit 0 only", false);
			return;
		}
	}
	checkTrue("telem bit is bit 0 only", true);

	// The payload must be what sendThrottle() would have produced, so enabling
	// telemetry cannot change the commanded throttle.
	for (uint16_t t = 0; t <= 2000; t += 211) {
		uint16_t expect = (uint16_t)((t ? t + 47 : 0) << 1);
		if (kissBuildDshotPayload(t, false) != expect) {
			checkTrue("matches sendThrottle() encoding", false);
			return;
		}
	}
	checkTrue("matches sendThrottle() encoding", true);
}

/** @brief An EDT-only telemetry block: every EDT field present, no KISS. */
static void edtOnly(EscTelemetry *t) {
	memset(t, 0, sizeof(*t));
	t->volts = 12.25f; t->edtVoltsMs = 1;   // EDT resolution: 0.25 V steps
	t->amps  = 3.0f;   t->edtAmpsMs  = 1;   // EDT resolution: whole amps
	t->tempC = 40;     t->edtTempMs  = 1;
	t->erpm  = 70000;
	t->rpm   = 10000;
}

/**
 * @brief Wide enough that EDT never expires during a test.
 *
 * The KISS-preference tests call escMerge() at scattered timestamps, and EDT
 * expiry is not what they are about. Its own edge is testEdtStaleness().
 */
static const uint32_t EDT_NEVER = 1000000u;

/** @brief Per-field preference and the staleness edge. */
static void testMerge() {
	section("Merging KISS with EDT");

	EscTelemetry t;
	EscReading r;

	// No KISS at all: everything falls back to EDT.
	edtOnly(&t);
	escMerge(&t, 1000, 500, EDT_NEVER, &r);
	checkTrue("EDT used when KISS absent", r.voltsFrom == EscSource::Edt);
	checkInt("EDT volts x100", (long)(r.volts * 100.0f + 0.5f), 1225);
	checkTrue("no mAh without KISS", r.mahFrom == EscSource::None);
	checkInt("mAh reads zero", r.mah, 0);
	checkTrue("kiss not fresh", !r.kissFresh);

	// A fresh KISS frame takes over the electrical fields.
	t.haveKiss = true;
	t.kissLastMs = 900;
	t.kissVolts = 12.37f;
	t.kissAmps = 3.42f;
	t.kissTempC = 41;
	t.kissMah = 250;
	t.kissErpm = 69900;
	escMerge(&t, 1000, 500, EDT_NEVER, &r);
	checkTrue("KISS preferred when fresh", r.voltsFrom == EscSource::Kiss);
	checkInt("KISS volts x100", (long)(r.volts * 100.0f + 0.5f), 1237);
	checkInt("KISS amps x100", (long)(r.amps * 100.0f + 0.5f), 342);
	checkInt("KISS temperature", r.tempC, 41);
	checkTrue("mAh appears", r.mahFrom == EscSource::Kiss);
	checkInt("mAh value", r.mah, 250);

	// RPM is never taken from KISS, however fresh it is.
	checkInt("rpm still from DShot", r.rpm, 10000);
	checkInt("erpm still from DShot", r.erpm, 70000);
	checkInt("KISS erpm carried alongside", r.kissErpm, 69900);
	checkTrue("KISS erpm flagged present", r.haveKissErpm);

	// The staleness edge. 499 ms old is fresh, 500 is not.
	escMerge(&t, 900 + 499, 500, EDT_NEVER, &r);
	checkTrue("499 ms old is fresh", r.voltsFrom == EscSource::Kiss);
	escMerge(&t, 900 + 500, 500, EDT_NEVER, &r);
	checkTrue("500 ms old is stale", r.voltsFrom == EscSource::Edt);
	checkInt("falls back to EDT volts", (long)(r.volts * 100.0f + 0.5f), 1225);

	// Consumption goes to zero rather than freezing. A stale mAh sitting beside
	// a live voltage would read as "the motor stopped drawing current".
	checkTrue("stale mAh is dropped", r.mahFrom == EscSource::None);
	checkInt("stale mAh reads zero", r.mah, 0);
	checkTrue("stale KISS erpm dropped", !r.haveKissErpm);

	// KISS present but EDT never seen: stale KISS leaves nothing behind.
	memset(&t, 0, sizeof(t));
	t.haveKiss = true; t.kissLastMs = 0; t.kissVolts = 12.0f;
	escMerge(&t, 5000, 500, EDT_NEVER, &r);
	checkTrue("no source at all", r.voltsFrom == EscSource::None);
	checkInt("reads zero, not stale data", (long)(r.volts * 100.0f), 0);
}

/** @brief millis() wraps every 49 days; the staleness test must survive it. */
static void testMergeWraparound() {
	section("Staleness across millis() rollover");

	EscTelemetry t;
	EscReading r;
	edtOnly(&t);
	t.haveKiss = true;

	// Frame arrived 100 ms before the counter wrapped; "now" is 50 ms after.
	t.kissLastMs = 0xFFFFFF9Cu;      // -100 as unsigned
	escMerge(&t, 50, 500, EDT_NEVER, &r);
	checkTrue("150 ms across the wrap is fresh", r.voltsFrom == EscSource::Kiss);

	// Same frame, but now 600 ms have passed across the wrap.
	escMerge(&t, 500, 500, EDT_NEVER, &r);
	checkTrue("600 ms across the wrap is stale", r.voltsFrom == EscSource::Edt);
}

/**
 * @brief EDT fields expire; they do not hold their last value forever.
 *
 * This is the bug the timestamps replaced `have*` flags for. A boolean that
 * only ever goes true meant unplugging the ESC, or swapping it for a different
 * one, left the previous readings on screen looking entirely live.
 */
static void testEdtStaleness() {
	section("EDT expiry");

	EscTelemetry t;
	EscReading r;
	edtOnly(&t);                       // stamps every EDT field at t=1
	t.stress = 77;
	t.edtStressMs = 1;
	t.warning = true;
	t.alert   = true;
	t.error   = true;
	t.maxStress = 9;
	t.edtStatusMs = 1;

	escMerge(&t, 1 + 999, 500, 1000, &r);
	checkTrue("999 ms old is fresh", r.voltsFrom == EscSource::Edt);
	checkInt("and carries its value", (long)(r.volts * 100.0f + 0.5f), 1225);
	checkTrue("stress fresh", r.stressFrom == EscSource::Edt);
	checkInt("stress value", r.stress, 77);
	checkTrue("status fresh", r.statusFrom == EscSource::Edt);
	checkTrue("warning flag survives", r.warning);
	checkTrue("alert flag survives", r.alert);
	checkTrue("error flag survives", r.error);
	checkInt("max stress survives", r.maxStress, 9);
	checkTrue("edtFresh set", r.edtFresh);

	escMerge(&t, 1 + 1000, 500, 1000, &r);
	checkTrue("1000 ms old is stale", r.voltsFrom == EscSource::None);
	checkInt("value is dropped, not held", (long)(r.volts * 100.0f), 0);
	checkTrue("amps dropped", r.ampsFrom == EscSource::None);
	checkTrue("temp dropped", r.tempFrom == EscSource::None);
	checkTrue("stress dropped", r.stressFrom == EscSource::None);
	checkInt("stress reads zero", r.stress, 0);
	checkTrue("edtFresh clear", !r.edtFresh);

	// The direction that matters most: an expired status block must not read
	// as all-clear. A warning that quietly becomes OK is worse than no
	// indicator at all.
	checkTrue("status source gone", r.statusFrom == EscSource::None);
	checkTrue("warning not silently cleared to OK", !r.warning);
	// All three status bits, not just the one. `warning` was the only flag any
	// test asserted, so `alert` and `error` could each be made to survive expiry
	// without a single check noticing -- and of the three, alert is the one that
	// most matters to leave stuck on a screen after the ESC has gone.
	checkTrue("nor is alert left set after expiry", !r.alert);
	checkTrue("nor error", !r.error);
	checkTrue("and the status source reads as absent",
	          r.statusFrom == EscSource::None);
	checkInt("with max stress cleared too", r.maxStress, 0);

	// Expiry is per field: one frame type stopping does not blank the rest.
	edtOnly(&t);
	t.edtTempMs = 1;
	t.edtVoltsMs = 900;
	escMerge(&t, 1500, 500, 1000, &r);
	checkTrue("voltage still fresh", r.voltsFrom == EscSource::Edt);
	checkTrue("temperature expired alone", r.tempFrom == EscSource::None);

	// Never-seen is not the same as stale, and must not read fresh at boot --
	// stamp 0 against a nowMs of 0 is the case a bare subtraction gets wrong.
	memset(&t, 0, sizeof(t));
	escMerge(&t, 0, 500, 1000, &r);
	checkTrue("unstamped field is not fresh at t=0", r.voltsFrom == EscSource::None);
	checkTrue("escFieldFresh rejects a zero stamp", !escFieldFresh(0, 0, 1000));
	checkTrue("escFieldFresh accepts a real one", escFieldFresh(1, 500, 1000));
}

/**
 * @brief The automatic EDT enable follows the ESC, not the clock, and keeps
 *        asking until it actually works.
 *
 * Two earlier versions failed the same way. The first fired once, 1.5 s after
 * boot, so only an ESC already plugged in and powered ever got it. The second
 * fired once on the first eRPM frame -- which fixed "plugged in later" and kept
 * the one-shot, at the earliest instant an ESC can be heard from and therefore
 * close to the least likely instant it will act on a command. From the outside
 * both look identical: an ESC reporting RPM and nothing else, exactly like one
 * with no EDT support at all.
 */
static void testEdtAutoEnable() {
	section("Automatic EDT enable");

	const uint32_t RETRY = 1000;

	// Nothing connected: nothing to do, however long we wait.
	checkTrue("no ESC, nothing sent",
	          edtAutoAction(false, false, false, 0, RETRY) == EdtAutoAction::None);

	// An ESC answers eRPM but sends no EDT.
	checkTrue("ESC appears, enable is sent",
	          edtAutoAction(true, false, false, 0, RETRY) == EdtAutoAction::Send);
	checkTrue("not again on the next frame",
	          edtAutoAction(true, false, true, 1, RETRY) == EdtAutoAction::None);
	checkTrue("nor a moment before the interval is up",
	          edtAutoAction(true, false, true, RETRY - 1, RETRY) == EdtAutoAction::None);

	// This is the fix: an enable that was not acted on goes out again.
	checkTrue("but again once the interval has passed",
	          edtAutoAction(true, false, true, RETRY, RETRY) == EdtAutoAction::Send);

	// And this is the success condition -- EDT frames arriving, not an enable
	// having been sent. The two came apart exactly once and cost a bug report.
	checkTrue("EDT arriving stops the asking",
	          edtAutoAction(true, true, true, RETRY * 10, RETRY) == EdtAutoAction::None);
	checkTrue("and nothing is sent to an ESC already sending EDT",
	          edtAutoAction(true, true, false, 0, RETRY) == EdtAutoAction::None);

	// It goes away. The state must re-arm, or the replacement never gets one.
	checkTrue("ESC leaves, the attempt re-arms",
	          edtAutoAction(false, false, true, 0, RETRY) == EdtAutoAction::Rearm);
	checkTrue("and re-arms once, not every frame",
	          edtAutoAction(false, false, false, 0, RETRY) == EdtAutoAction::None);

	// Full cycle through the state the firmware actually keeps: an ESC that
	// ignores the enable, then takes it, then is swapped for another.
	bool tried = false, edt = false;
	uint32_t now = 0, lastTry = 0;
	int sends = 0;
	auto step = [&](bool linkUp) {
		now += 100;
		switch (edtAutoAction(linkUp, edt, tried, now - lastTry, RETRY)) {
			case EdtAutoAction::Send:  tried = true; lastTry = now; sends++; break;
			case EdtAutoAction::Rearm: tried = false;                        break;
			case EdtAutoAction::None:                                        break;
		}
	};

	for (int i = 0; i < 50; i++) step(true);     // 5 s of an ESC ignoring it
	checkTrue("an ESC that does not answer is asked more than once", sends > 1);
	checkInt("about once per interval, not once per frame", sends, 5, 1);

	edt = true;                                  // it finally took
	int atSuccess = sends;
	for (int i = 0; i < 50; i++) step(true);
	checkInt("and the asking stops the moment EDT arrives", sends, atSuccess);

	edt = false;
	for (int i = 0; i < 50; i++) step(false);    // unplugged
	checkInt("none while nothing is connected", sends, atSuccess);
	for (int i = 0; i < 5; i++) step(true);      // replacement fitted
	checkTrue("the replacement is asked straight away", sends > atSuccess);
}

/** @brief Source labels, which the UI prints verbatim. */
static void testSourceLabels() {
	section("Source labels");
	checkStr("KISS", escSourceLabel(EscSource::Kiss), "KISS");
	checkStr("EDT",  escSourceLabel(EscSource::Edt),  "EDT");
	checkStr("none", escSourceLabel(EscSource::None), "--");
}

/** @brief Run every KISS suite. */
void runKissTests() {
	testCrc();
	testDecode();
	testRanges();
	testCrcRejection();
	testStreaming();
	testStreamingRecovery();
	testResync();
	testPayloadBuilder();
	testMerge();
	testMergeWraparound();
	testEdtStaleness();
	testEdtAutoEnable();
	testSourceLabels();
}
