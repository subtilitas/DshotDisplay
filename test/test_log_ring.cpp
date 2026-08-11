/**
 * @file test_log_ring.cpp
 * @brief Host tests for the log ring buffer.
 *
 * Wraparound and the all-or-nothing rule are where a ring buffer goes wrong,
 * and both are hard to notice on a device: the symptom is a log that decodes
 * for a while and then desynchronises.
 */

#include "check.h"

#include "../src/log_ring.h"

#include <string.h>

/** @brief Drain everything, checking the bytes come back in order. */
static bool drainAndVerify(LogRing *r, const uint8_t *expect, uint32_t len) {
	uint32_t got = 0;
	while (true) {
		const uint8_t *p = nullptr;
		uint32_t n = logRingPeek(r, &p);
		if (n == 0) break;
		if (got + n > len) return false;
		if (memcmp(p, expect + got, n) != 0) return false;
		got += n;
		logRingConsume(r, n);
	}
	return got == len;
}

/** @brief Empty, full, and the counters in between. */
static void testBasics() {
	section("Ring basics");

	uint8_t storage[16];
	LogRing r;
	logRingInit(&r, storage, sizeof(storage));

	checkInt("starts empty", logRingUsed(&r), 0);
	checkInt("all free", logRingFree(&r), 16);

	const uint8_t data[] = {1, 2, 3, 4, 5};
	checkTrue("small write accepted", logRingWrite(&r, data, 5));
	checkInt("used reflects it", logRingUsed(&r), 5);
	checkInt("free reflects it", logRingFree(&r), 11);
	checkInt("accepted counter", r.accepted, 5);

	checkTrue("bytes come back in order", drainAndVerify(&r, data, 5));
	checkInt("empty again", logRingUsed(&r), 0);

	// head == tail means empty *or* full, hence the explicit flag. Filling
	// exactly to capacity is the case that gets this wrong.
	uint8_t big[16];
	for (int i = 0; i < 16; i++) big[i] = (uint8_t)(i + 100);
	checkTrue("exact-capacity write accepted", logRingWrite(&r, big, 16));
	checkInt("reads as full, not empty", logRingUsed(&r), 16);
	checkInt("no free space", logRingFree(&r), 0);
	checkTrue("full buffer drains correctly", drainAndVerify(&r, big, 16));
	checkInt("empty after draining a full ring", logRingUsed(&r), 0);
}

/** @brief A frame that does not fit is refused whole. */
static void testAllOrNothing() {
	section("All-or-nothing writes");

	uint8_t storage[16];
	LogRing r;
	logRingInit(&r, storage, sizeof(storage));

	const uint8_t ten[10] = {1,2,3,4,5,6,7,8,9,10};
	checkTrue("first 10 fit", logRingWrite(&r, ten, 10));

	// Only 6 bytes free. A general ring would take 6 and report a short write;
	// this must take none, or the log stream gets a truncated frame spliced
	// onto whatever comes next.
	checkTrue("10 more are refused", !logRingWrite(&r, ten, 10));
	checkInt("nothing was written", logRingUsed(&r), 10);
	checkInt("dropped bytes counted", r.dropped, 10);
	checkInt("drop events counted", r.drops, 1);

	// A smaller one still fits, so a refusal must not wedge the buffer.
	const uint8_t six[6] = {11,12,13,14,15,16};
	checkTrue("a fitting write still succeeds", logRingWrite(&r, six, 6));
	checkInt("now exactly full", logRingUsed(&r), 16);

	uint8_t expect[16];
	memcpy(expect, ten, 10);
	memcpy(expect + 10, six, 6);
	checkTrue("contents are the two good writes", drainAndVerify(&r, expect, 16));
}

/** @brief Writes that straddle the end of the storage array. */
static void testWraparound() {
	section("Wraparound");

	uint8_t storage[16];
	LogRing r;
	logRingInit(&r, storage, sizeof(storage));

	// Push the head near the end, then drain, so the next write must split.
	uint8_t pad[12];
	memset(pad, 0xAA, sizeof(pad));
	logRingWrite(&r, pad, 12);
	logRingConsume(&r, 12);
	checkInt("head parked near the end", r.head, 12);

	uint8_t data[10];
	for (int i = 0; i < 10; i++) data[i] = (uint8_t)(i + 1);
	checkTrue("straddling write accepted", logRingWrite(&r, data, 10));
	checkInt("used is correct across the seam", logRingUsed(&r), 10);

	// Peek must stop at the array end, so this needs two reads.
	const uint8_t *p = nullptr;
	uint32_t n = logRingPeek(&r, &p);
	checkInt("first peek stops at the seam", n, 4);
	checkTrue("first run is correct", memcmp(p, data, 4) == 0);
	logRingConsume(&r, n);

	n = logRingPeek(&r, &p);
	checkInt("second peek returns the rest", n, 6);
	checkTrue("second run is correct", memcmp(p, data + 4, 6) == 0);
	logRingConsume(&r, n);
	checkInt("empty after both runs", logRingUsed(&r), 0);

	// Many cycles of partial fill and drain, to catch an index that creeps.
	logRingInit(&r, storage, sizeof(storage));
	bool ok = true;
	uint8_t seq = 0;
	for (int cycle = 0; cycle < 200; cycle++) {
		uint8_t out[5], back[5];
		for (int i = 0; i < 5; i++) out[i] = seq++;
		if (!logRingWrite(&r, out, 5)) { ok = false; break; }

		uint32_t got = 0;
		while (got < 5) {
			const uint8_t *q = nullptr;
			uint32_t m = logRingPeek(&r, &q);
			if (m == 0) break;
			if (m > 5 - got) m = 5 - got;
			memcpy(back + got, q, m);
			got += m;
			logRingConsume(&r, m);
		}
		if (got != 5 || memcmp(out, back, 5) != 0) { ok = false; break; }
	}
	checkTrue("200 fill/drain cycles stay consistent", ok);
}

/** @brief The high-water mark, which is how the buffer gets sized honestly. */
static void testPeak() {
	section("High-water mark");

	uint8_t storage[64];
	LogRing r;
	logRingInit(&r, storage, sizeof(storage));

	uint8_t data[20];
	memset(data, 0x5A, sizeof(data));

	logRingWrite(&r, data, 20);
	checkInt("peak tracks the first write", r.peakUsed, 20);
	logRingWrite(&r, data, 20);
	checkInt("peak rises with occupancy", r.peakUsed, 40);
	logRingConsume(&r, 40);
	checkInt("peak survives draining", r.peakUsed, 40);
	logRingWrite(&r, data, 10);
	checkInt("a smaller peak does not lower it", r.peakUsed, 40);
}

/** @brief Degenerate sizes must fail cleanly rather than divide by zero. */
static void testZeroSize() {
	section("Zero-sized ring");

	LogRing r;
	logRingInit(&r, nullptr, 0);

	const uint8_t data[4] = {1, 2, 3, 4};
	checkTrue("write is refused, not crashed", !logRingWrite(&r, data, 4));
	checkInt("counted as dropped", r.dropped, 4);
	checkInt("still reads empty", logRingUsed(&r), 0);

	const uint8_t *p = nullptr;
	checkInt("peek returns nothing", logRingPeek(&r, &p), 0);
	logRingConsume(&r, 10);   // must not trap
	checkTrue("consume on empty is harmless", true);
}

/** @brief Run every ring suite. */
void runLogRingTests() {
	testBasics();
	testAllOrNothing();
	testWraparound();
	testPeak();
	testZeroSize();
}
