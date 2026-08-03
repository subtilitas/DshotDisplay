/**
 * @file test_dump.cpp
 * @brief The bridge log must print every record it claims to hold.
 *
 * A dump that announces 14 records and prints 6 is worse than no dump: it
 * looks complete, so the missing lines get blamed on whoever pasted it. That
 * happened. The fix was pacing the USB writes; this asserts the outcome by
 * counting the lines that actually reached the host, not by reading the code.
 */

#include "check.h"
#include "fakes.h"
#include "bridge_log.h"

#include <stdio.h>
#include <string.h>

/** @brief Newlines in everything the board wrote to USB. */
static int linesDelivered() {
	static uint8_t buf[16384];
	int n = fakeHostReceived(buf, sizeof(buf));
	int lines = 0;
	for (int i = 0; i < n; i++) if (buf[i] == '\n') lines++;
	return lines;
}

void runDumpTest() {
	section("Bridge log: the dump is complete");

	const uint8_t init[21] = {0,0,0,0,0,0,0,0,0,0,0,0,
	                          0x0D,'B','L','H','e','l','i',0xF4,0x7D};
	const uint8_t rep[9]   = {0x34,0x37,0x31,0x02,0x35,0x06,0x06,0x02,0x30};

	// The reported session: 14 records, the longest 21 bytes of payload.
	fakeHostClear();
	bridgeLogReset();
	bridgeLogNote("bridge opened");
	for (int i = 0; i < 6; i++) {
		fakeAdvance(500);
		bridgeLogAdd(BLOG_HOST_TO_ESC, init, sizeof(init));
		bridgeLogAdd(BLOG_ESC_TO_HOST, rep, sizeof(rep));
	}
	bridgeLogNote("bridge closed");
	checkInt("holds 14 records", bridgeLogCount(), 14);

	bridgeLogDump();
	// blank + title + column header, 14 records, END + blank.
	checkInt("every record reaches the host", linesDelivered(), 14 + 5);

	// A long session must still dump whatever survived, not stop early.
	fakeHostClear();
	bridgeLogReset();
	for (int i = 0; i < 400; i++) {
		fakeAdvance(5);
		bridgeLogAdd(BLOG_HOST_TO_ESC, init, sizeof(init));
	}
	uint16_t held = bridgeLogCount();
	checkTrue("ring dropped oldest as designed", bridgeLogOverflowed());
	bridgeLogDump();
	checkInt("long dump is complete too", linesDelivered(), held + 5);
}
