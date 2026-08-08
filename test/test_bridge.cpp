/**
 * @file test_bridge.cpp
 * @brief USB bridge tests, driven through the real forwarding loop.
 *
 * usb_bridge.cpp and bridge_wire.cpp are included rather than linked so a fake
 * one-wire line can be substituted for am32WriteRaw/am32ReadRaw without those
 * becoming function pointers in production code.
 *
 * On hardware these two files run on different cores. Here there is one thread,
 * so the tests drive each core explicitly -- which is better than real
 * concurrency for this purpose: it makes "core0 was away for 50 ms" something a
 * test can state exactly and reproduce every run. @see pump()
 */

#include "check.h"
#include "fakes.h"
#include "bridge_log.h"
#include "am32_bl.h"

#include <stdint.h>
#include <string.h>

// --- fake one-wire line, standing in for the ESC ---
//
// The important property is that this models a *wire*, not a buffered UART. On
// the real link the ESC transmits at a particular moment and the bits are gone
// afterwards: if no one is inside am32ReadRaw() when they go past, they are lost,
// not queued. A fake that holds the reply until someone asks for it cannot fail
// on the bug this file exists to catch -- an earlier version did exactly that,
// and happily passed against a bridge that dropped every late reply.
static uint8_t  g_wire[512];     /**< What the board transmitted. */
static int      g_wireLen = 0;
static uint8_t  g_reply[64];     /**< What the ESC will answer with. */
static int      g_replyLen = 0;
static bool     g_armed = false; /**< ESC will answer the next command. */
static uint32_t g_replyDelay = 0;/**< How long it takes to answer, in ms. */
static uint32_t g_replyAt = 0;   /**< When the bytes are on the wire. */
static bool     g_replyPending = false;
static uint32_t g_lost = 0;      /**< Replies that went past unheard. */

/** @brief Runs core0 while core1 is transmitting. @see am32WriteRawEchoed() */
static void (*g_core0Hook)() = nullptr;

// --- a model of the reference tool's receive loop, driven by its own clock ---
static uint32_t g_hostNextMs = 0;    /**< When the host next wakes to read. */
static bool     g_hostAcked  = false;
static int      g_hostReads  = 0;    /**< read_all() calls that returned bytes. */
static int      g_hostDiscards = 0;  /**< Reads thrown away for not ending 0x30. */

/**
 * @brief Transmit, charging the caller real wire time for it.
 *
 * A byte at 19200 8N1 is ten bits, ~521 us. The virtual clock is whole
 * milliseconds, so time is charged every other byte -- close enough to model
 * the thing that matters, which is that a long frame occupies the wire for a
 * long time and its echo comes back spread over that period.
 */
void am32WriteRawEchoed(const uint8_t *d, uint16_t n, Am32ByteSink sink) {
	for (uint16_t i = 0; i < n; i++) {
		if (g_wireLen < (int)sizeof(g_wire)) g_wire[g_wireLen++] = d[i];
		if (i & 1) fakeAdvance(1);
		if (sink) sink(d[i]);

		// Core0 is a separate core and keeps draining the ring while this
		// transmission is in progress. Without this the harness would run the
		// whole frame out before core0 ever got a turn, core0 would forward the
		// echo in one gulp, and the wire pacing this models would be destroyed
		// by the test rather than by the code.
		if (g_core0Hook && (i & 1)) g_core0Hook();
	}

	// The ESC answers a command it receives -- after its own processing time.
	// PROG_FLASH is the slow one: it erases and programs before acknowledging.
	if (g_armed) {
		g_replyAt = fakeNow() + g_replyDelay;
		g_replyPending = true;
	}
}
void am32WriteRaw(const uint8_t *d, uint16_t n) {
	am32WriteRawEchoed(d, n, nullptr);
}

/**
 * @brief Stand-in for the one-wire read, faithful to its timing contract.
 *
 * Honours the window in both directions: a silent ESC costs the caller the
 * whole window, and a reply that fell outside the window is *gone*.
 */
uint16_t am32ReadRaw(uint8_t *buf, uint16_t maxLen, uint32_t windowMs) {
	uint32_t start = fakeNow();
	uint32_t end   = start + windowMs;

	if (!g_replyPending) { fakeAdvance(windowMs); return 0; }

	if (g_replyAt < start) {
		// It went past while this caller was elsewhere. On a bit-banged link
		// that is a permanent loss, which is the whole point of the test.
		g_replyPending = false;
		g_lost++;
		fakeAdvance(windowMs);
		return 0;
	}
	if (g_replyAt > end) { fakeAdvance(windowMs); return 0; }   // not yet

	fakeAdvance(g_replyAt - start + 1);
	int n = g_replyLen < (int)maxLen ? g_replyLen : maxLen;
	memcpy(buf, g_reply, n);
	g_replyPending = false;
	return (uint16_t)n;
}

// am32BlBegin/am32BlEnd, which bridge_wire.cpp uses to claim the pin, come
// from fakes.cpp.
#include "../src/bridge_wire.cpp"  // NOLINT -- "core1"
#include "../src/usb_bridge.cpp"   // NOLINT -- "core0"

/**
 * @brief One step of core0 plus the host, both on their own schedules.
 *
 * The host wakes every 25 ms and calls read_all() no matter what the board is
 * doing -- including part-way through a 134 ms transmission. Counting loop
 * iterations instead of milliseconds hides exactly the bug being tested: the
 * whole transmit happens inside one iteration, so the echo and the ACK land in
 * the same notional "window" and any bridge looks correct.
 */
static void hostAndCore0() {
	usbBridgePoll();                                  // core0

	if ((int32_t)(fakeNow() - g_hostNextMs) < 0) return;
	g_hostNextMs = fakeNow() + 25;                    // time.sleep(0.025)

	static uint8_t big[512];
	int got = fakeHostDrain(big, sizeof(big));        // read_all()
	if (got == 0) return;
	g_hostReads++;

	if (got > 1) {                                    // if len(...) > 1
		if (big[got - 1] == 0x30) g_hostAcked = true; //   if result[-1] == 0x30
		else                      g_hostDiscards++;   //   else: discarded
	} else {
		g_hostDiscards++;                             // a lone byte is ignored
	}
}

/** @brief Run core1 alone, as if core0 were busy repainting. */
static void core1(int passes = 1) {
	for (int i = 0; i < passes; i++) bridgeWirePoll();
}

/**
 * @brief Run both cores for one round, in the order hardware would.
 *
 * core0 queues a frame, core1 puts it on the wire and collects the answer,
 * core0 forwards that answer to USB.
 */
static void pump(int rounds = 1) {
	for (int i = 0; i < rounds; i++) {
		usbBridgePoll();
		bridgeWirePoll();
		usbBridgePoll();
	}
}

/** @brief Arm the ESC to answer the next command after @p delayMs. */
static void escWillReplyAfter(const uint8_t *d, int n, uint32_t delayMs) {
	if (d && n) memcpy(g_reply, d, n);
	g_replyLen     = n;
	g_armed        = (d && n);
	g_replyDelay   = delayMs;
	g_replyPending = false;
}
/** @brief Arm the ESC to answer the next command promptly, as a read does. */
static void escWillReply(const uint8_t *d, int n) { escWillReplyAfter(d, n, 0); }
static void reset() {
	g_wireLen = 0;
	g_replyLen = 0;
	g_armed = false;
	g_replyPending = false;
	g_lost = 0;
	fakeHostClear();
}

void runBridgeTests() {
	section("USB bridge");

	usbBridgeBegin(4);
	checkTrue("bridge is active", usbBridgeActive());

	// A real SET_ADDRESS frame, answered with a bare ACK.
	const uint8_t setAddr[] = {0xFF, 0x00, 0x7C, 0x00, 0x3C, 0x8B};
	const uint8_t ack[]     = {0x30};

	reset();
	fakeHostSend(setAddr, sizeof(setAddr));
	escWillReply(ack, sizeof(ack));
	for (int i = 0; i < 4; i++) { fakeAdvance(3); pump(); }

	checkInt("frame reached the ESC intact", g_wireLen, (long)sizeof(setAddr));
	checkTrue("bytes on the wire match",
	          memcmp(g_wire, setAddr, sizeof(setAddr)) == 0);

	uint8_t host[128];
	int hn = fakeHostReceived(host, sizeof(host));

	// The whole point. A desktop tool checks `len(result) > 1 and result[-1] ==
	// 0x30`. Without the echo the reply is one byte, len > 1 fails, and the
	// host reports NACK on the first command of every session.
	checkInt("host sees echo + reply", hn, (long)(sizeof(setAddr) + 1));
	checkTrue("echo precedes the reply",
	          memcmp(host, setAddr, sizeof(setAddr)) == 0);
	checkInt("last byte is the ACK", host[hn - 1], 0x30);
	checkTrue("reference tool's len>1 test passes", hn > 1);

	// Both reach the host within one of its 25 ms polls, which is all that is
	// required -- and all a plain USB-serial adapter provides.

	section("USB bridge: reply framing the host relies on");
	{
		// A READ_FLASH result: data, CRC low, CRC high, ACK. The tool slices
		// from the end, so leading echo must not disturb the offsets.
		reset();
		const uint8_t cmd[] = {0x03, 0x30, 0x00, 0x00};
		uint8_t rep[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x30};
		fakeHostSend(cmd, sizeof(cmd));
		escWillReply(rep, sizeof(rep));
		for (int i = 0; i < 4; i++) { fakeAdvance(3); pump(); }

		hn = fakeHostReceived(host, sizeof(host));
		checkInt("total = echo + reply", hn, (long)(sizeof(cmd) + sizeof(rep)));
		checkInt("result[-1] is the ACK", host[hn - 1], 0x30);
		checkInt("result[-5] is the ESC type slot", host[hn - 5], 0xDD);
		checkTrue("data slice lands correctly",
		          host[hn - 8] == 0xAA && host[hn - 7] == 0xBB);

	}

	section("USB bridge: traffic log");
	{
		reset();
		usbBridgeBegin(4);
		const uint8_t greet[] = {0x00,0x00,0x0D,'B','L','H','e','l','i',0xF4,0x7D};
		const uint8_t info[]  = {0x34,0x37,0x31,0x00,0x1F,0x06,0x06,0x01,0x30};
		fakeHostSend(greet, sizeof(greet));
		escWillReply(info, sizeof(info));
		for (int i = 0; i < 4; i++) { fakeAdvance(3); pump(); }

		checkTrue("log captured both directions", bridgeLogCount() >= 3);
		checkTrue("nothing dropped in a short session", !bridgeLogOverflowed());

		printf("\n    --- sample of what you would paste back ---\n");
		fakeSerialPrint(true);
		usbBridgeEnd();          // dumps
		fakeSerialPrint(false);
	}

	section("USB bridge: an unacknowledged command must not stall");
	{
		// SET_BUFFER is never acknowledged by the bootloader. A bridge that
		// waits for a reply blocks its whole timeout here, so the echo reaches
		// the host after its flushInput() and lands in the payload exchange --
		// which is exactly how firmware writes broke while reads kept working.
		reset();
		usbBridgeBegin(4);
		const uint8_t setBuf[] = {0xFE, 0x00, 0x00, 0x30, 0x8A, 0x1C};
		fakeHostSend(setBuf, sizeof(setBuf));
		escWillReply(nullptr, 0);          // deliberately silent

		uint32_t before = fakeNow();
		fakeAdvance(1);
		pump();
		uint32_t elapsed = fakeNow() - before;

		int stalled = fakeHostReceived(host, sizeof(host));
		checkInt("echo delivered despite no reply", stalled, (long)sizeof(setBuf));
		checkTrue("bytes reached the ESC", g_wireLen == (int)sizeof(setBuf));
		// The frame's own wire time is unavoidable; a reply timeout on top of it
		// is not. Anything approaching one means the transactional model has
		// crept back in.
		printf("    [round took %lums for a %u-byte frame]\n",
		       (unsigned long)elapsed, (unsigned)sizeof(setBuf));
		checkTrue("did not stall waiting for a reply", elapsed < 20);

		usbBridgeEnd();
	}

	section("USB bridge: a late reply during a repaint");
	{
		// The bug that killed firmware uploads part-way through.
		//
		// PROG_FLASH is not answered immediately: the bootloader erases and
		// programs a page first, then acknowledges tens of milliseconds later.
		// Meanwhile core0 is repainting the bridge screen -- millisecond upon
		// millisecond of SPI DMA. While the wire lived on core0, those ACKs went
		// past while nobody was listening and were lost outright: an upload that
		// ran for a while and then died on whichever repaint collided with one.
		// Settings never showed it, because a read is answered inside the very
		// poll that sent it.
		reset();
		usbBridgeBegin(4);

		const uint8_t prog[] = {0x01, 0x01};
		escWillReplyAfter(ack, sizeof(ack), 20);   // ESC needs 20 ms to program
		fakeHostSend(prog, sizeof(prog));
		usbBridgePoll();                           // core0 queues the frame
		core1();                                   // core1 clocks it out

		// core0 now disappears into a long repaint. Core1 keeps the wire.
		for (int i = 0; i < 40; i++) { fakeAdvance(1); core1(); }

		checkInt("the ACK was not missed", (long)g_lost, 0);
		checkInt("nothing overran the ring", (long)bridgeWireOverruns(), 0);

		// core0 finally gets a turn: echo then ACK, in order.
		usbBridgePoll();
		hn = fakeHostReceived(host, sizeof(host));
		checkInt("echo and late ACK both survived", hn, (long)(sizeof(prog) + 1));
		if (hn > 0) checkInt("and it ends in the ACK", host[hn - 1], 0x30);

		usbBridgeEnd();
	}

	section("USB bridge: the ring absorbs a whole late reply");
	{
		// Same shape, but a multi-byte reply, to show the ring is buffering
		// rather than merely holding the last thing it happened to see.
		reset();
		usbBridgeBegin(4);

		const uint8_t cmd2[] = {0x03, 0x08, 0x00, 0x00};
		const uint8_t page[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22, 0x33, 0x30};
		escWillReplyAfter(page, sizeof(page), 25);
		fakeHostSend(cmd2, sizeof(cmd2));
		usbBridgePoll();
		core1();

		for (int i = 0; i < 40; i++) { fakeAdvance(1); core1(); }

		checkInt("reply not missed", (long)g_lost, 0);
		usbBridgePoll();
		hn = fakeHostReceived(host, sizeof(host));
		checkInt("echo + whole reply preserved", hn,
		         (long)(sizeof(cmd2) + sizeof(page)));
		checkTrue("reply bytes are intact and in order",
		          hn == (int)(sizeof(cmd2) + sizeof(page)) &&
		          memcmp(host + sizeof(cmd2), page, sizeof(page)) == 0);

		usbBridgeEnd();
	}

	section("USB bridge: the reference tool's own ACK rule");
	{
		// A port of AM32Connector._receive_ack(), because paraphrasing it is
		// what let this bug through more than once:
		//
		//     for tries in range(50):
		//         time.sleep(0.025)
		//         self.last_result = self.serial_port.read_all()
		//         if len(self.last_result) > 1:
		//             if int(self.last_result[-1]) == 0x30: return True
		//             else: self.last_result = None
		//
		// Two rules matter and neither is obvious. A read of exactly one byte is
		// ignored, so a lone ACK can never be accepted. And a read that does not
		// end in 0x30 is *discarded*, not accumulated. Together they mean the
		// ACK must share a 25 ms window with at least one other byte.
		//
		// For a 256-byte payload -- 134 ms on the wire at 19200 baud -- what
		// satisfies that on real hardware is the tail of the echo still arriving
		// as the ESC answers. Echo the frame in one burst instead and the tool
		// discards it at the first window, then waits out a silence, then sees a
		// solitary ACK it will not accept.
		reset();
		usbBridgeBegin(4);

		uint8_t payload[258];              // 256 bytes of firmware plus CRC
		for (int i = 0; i < 258; i++) payload[i] = (uint8_t)(i * 7 + 3);
		escWillReplyAfter(ack, sizeof(ack), 4);

		fakeHostSend(payload, sizeof(payload));

		g_hostNextMs   = fakeNow() + 25;
		g_hostAcked    = false;
		g_hostReads    = 0;
		g_hostDiscards = 0;
		g_core0Hook    = hostAndCore0;     // core0 and the host run during the TX

		for (int i = 0; i < 4000 && !g_hostAcked; i++) { hostAndCore0(); core1(); }
		g_core0Hook = nullptr;

		printf("    [host reads: %d, discarded: %d]\n", g_hostReads, g_hostDiscards);
		checkTrue("the reference tool accepts the write", g_hostAcked);
		checkInt("no bytes lost on the way", (long)bridgeWireOverruns(), 0);

		usbBridgeEnd();
	}

	section("USB bridge: housekeeping");
	{
		reset();
		usbBridgeBegin(4);   // the log section closed it
		UsbBridgeStats st;
		usbBridgeStats(&st);
		uint32_t before = st.toEsc;
		const uint8_t junk[] = {1, 2, 3};
		fakeHostSend(junk, sizeof(junk));
		fakeAdvance(3); pump();
		usbBridgeStats(&st);
		checkInt("counters track forwarded bytes", (long)(st.toEsc - before), 3);

		// Stale bytes queued before the bridge opened would be forwarded to the
		// ESC as noise.
		fakeHostSend(junk, sizeof(junk));
		usbBridgeBegin(4);
		g_wireLen = 0;
		fakeAdvance(3); pump();
		checkInt("stale input flushed on open", g_wireLen, 0);

		usbBridgeEnd();
		checkTrue("bridge released", !usbBridgeActive());
	}
}
