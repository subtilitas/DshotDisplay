/**
 * @file bridge_wire.cpp
 * @brief Core1 one-wire pump for the USB bridge. @see bridge_wire.h
 */

#include "bridge_wire.h"
#include "am32_bl.h"

#include <Arduino.h>
#include <string.h>

/**
 * @brief How long one listening pass waits for a first byte, in milliseconds.
 *
 * Short, because core1 comes straight back round to listen again. It exists
 * only so a transmit queued meanwhile is not left waiting for long.
 */
#define WIRE_LISTEN_MS 1

/**
 * @brief Bytes taken from the wire in one listening pass.
 *
 * Only a staging size. Anything longer is picked up by the next pass, since
 * am32ReadRaw() returns as soon as the ESC stops talking.
 */
#define WIRE_RX_CHUNK 64

/**
 * @brief Longest bridgeWireEnd() will wait for core1 to let go, in ms.
 *
 * Must exceed the time a full frame takes to clock out, or leaving bridge mode
 * during a firmware write would pull the pin out from under a transmission in
 * progress. WIRE_FRAME_MAX bytes at 19200 baud 8N1 is ten bits each, so about
 * 167 ms; this rounds up and adds room for a listening pass.
 */
#define WIRE_TEARDOWN_MS 250

static_assert(WIRE_TEARDOWN_MS > (WIRE_FRAME_MAX * 10 * 1000) / 19200,
              "teardown can interrupt a full-length frame mid-transmission");

/*
 * Memory ordering
 * ---------------
 * Each index has exactly one writer, so no lock is needed, but the two cores
 * must still agree on ordering: the data must be visible before the index that
 * publishes it. __atomic_store_n with release, paired with acquire on the
 * reader, is what guarantees that. Plain volatile would not -- it constrains
 * the compiler but not the store buffer.
 */
#define WIRE_PUBLISH(var, val) __atomic_store_n(&(var), (val), __ATOMIC_RELEASE)
#define WIRE_OBSERVE(var)      __atomic_load_n(&(var), __ATOMIC_ACQUIRE)

static volatile bool s_wireActive = false;

/**
 * @brief Set by core1 while it is inside the wire routines.
 *
 * bridgeWireEnd() waits for this to clear before releasing the pin, so the
 * teardown cannot land in the middle of a byte.
 */
static volatile bool s_wireBusy = false;

// --- transmit: one frame slot, core0 fills, core1 drains ---
static uint8_t           s_txBuf[WIRE_FRAME_MAX];
static volatile uint16_t s_txLen     = 0;
static volatile bool     s_txPending = false;

// --- receive: SPSC byte ring, core1 writes head, core0 moves tail ---
static uint8_t           s_rxRing[WIRE_RX_RING];
static volatile uint16_t s_rxHead = 0;
static volatile uint16_t s_rxTail = 0;
static volatile uint32_t s_overruns = 0;

void bridgeWireBegin(uint8_t pin) {
	s_txPending = false;
	s_txLen     = 0;
	s_rxHead    = 0;
	s_rxTail    = 0;
	s_overruns  = 0;
	s_wireBusy      = false;

	am32BlBegin(pin);

	// Published last: core1 must not see the flag before the state it depends
	// on has been reset.
	WIRE_PUBLISH(s_wireActive, true);
}

void bridgeWireEnd() {
	WIRE_PUBLISH(s_wireActive, false);

	// Let core1 finish what it is in the middle of. Bounded, because a hung or
	// never-started core1 must not wedge the UI -- releasing the pin early is a
	// far smaller problem than never returning.
	for (int i = 0; i < WIRE_TEARDOWN_MS && WIRE_OBSERVE(s_wireBusy); i++) delay(1);

	am32BlEnd();
}

bool bridgeWireActive() { return WIRE_OBSERVE(s_wireActive); }

bool bridgeWireSend(const uint8_t *data, uint16_t len) {
	if (!len || len > WIRE_FRAME_MAX) return false;
	if (WIRE_OBSERVE(s_txPending)) return false;   // previous frame still queued

	memcpy(s_txBuf, data, len);
	s_txLen = len;
	WIRE_PUBLISH(s_txPending, true);               // publishes the buffer too
	return true;
}

uint16_t bridgeWireRecv(uint8_t *buf, uint16_t maxLen) {
	uint16_t head = WIRE_OBSERVE(s_rxHead);        // pairs with core1's publish
	uint16_t tail = s_rxTail;                      // core0 owns this one
	uint16_t n = 0;

	while (tail != head && n < maxLen) {
		buf[n++] = s_rxRing[tail];
		tail = (uint16_t)((tail + 1) % WIRE_RX_RING);
	}

	WIRE_PUBLISH(s_rxTail, tail);                  // frees the space for core1
	return n;
}

uint32_t bridgeWireOverruns() { return WIRE_OBSERVE(s_overruns); }

/**
 * @brief Push one byte into the receive ring. Core1 only.
 *
 * Used for both directions of the wire: the ESC's reply, and the echo of what
 * we transmit. They share the ring because on a real one-wire link they share
 * the conductor -- the host's receiver sees both, in the order they occurred.
 */
static void rxPush(uint8_t b) {
	uint16_t head = s_rxHead;                      // core1 owns this one
	uint16_t next = (uint16_t)((head + 1) % WIRE_RX_RING);
	if (next == WIRE_OBSERVE(s_rxTail)) {          // full: drop, and say so
		s_overruns = s_overruns + 1;
		return;
	}
	s_rxRing[head] = b;
	WIRE_PUBLISH(s_rxHead, next);
}

void bridgeWirePoll() {
	if (!WIRE_OBSERVE(s_wireActive)) {
		s_wireBusy = false;
		return;
	}

	WIRE_PUBLISH(s_wireBusy, true);

	// --- transmit ---
	// A whole frame in one call, so it goes out as one contiguous burst. Nothing
	// is received during it, which is correct for a half-duplex link: the ESC
	// does not answer until the command is complete.
	//
	// Each byte is fed back into the receive ring as it goes out, which is what
	// a shared conductor does for a real adapter. Reproducing the *pacing* is
	// the point -- see am32WriteRawEchoed().
	if (WIRE_OBSERVE(s_txPending)) {
		am32WriteRawEchoed(s_txBuf, s_txLen, rxPush);
		WIRE_PUBLISH(s_txPending, false);          // core0 may queue the next
	}

	// --- receive ---
	// The rest of the time is spent here. Core0's repaints no longer matter:
	// whatever arrives lands in the ring and waits to be collected.
	uint8_t  chunk[WIRE_RX_CHUNK];
	uint16_t r = am32ReadRaw(chunk, sizeof(chunk), WIRE_LISTEN_MS);

	for (uint16_t i = 0; i < r; i++) rxPush(chunk[i]);

	WIRE_PUBLISH(s_wireBusy, false);
}
