#include "log_ring.h"

#include <string.h>

/**
 * @file log_ring.cpp
 * @brief Ring buffer for the log byte stream. Pure; host-testable.
 */

void logRingInit(LogRing *r, uint8_t *buf, uint32_t size) {
	memset(r, 0, sizeof(*r));
	r->buf = buf;
	r->size = size;
}

void logRingReset(LogRing *r) {
	r->head = r->tail = 0;
	r->full = false;
}

uint32_t logRingUsed(const LogRing *r) {
	if (r->full) return r->size;
	if (r->head >= r->tail) return r->head - r->tail;
	return r->size - r->tail + r->head;
}

uint32_t logRingFree(const LogRing *r) {
	return r->size - logRingUsed(r);
}

bool logRingWrite(LogRing *r, const uint8_t *data, uint32_t len) {
	if (len == 0) return true;

	// All or nothing. Half a frame in the stream would splice two frames
	// together and leave the decoder resynchronising at an arbitrary offset.
	if (len > logRingFree(r)) {
		r->dropped += len;
		r->drops++;
		return false;
	}

	uint32_t first = r->size - r->head;
	if (first > len) first = len;
	memcpy(r->buf + r->head, data, first);
	if (len > first) memcpy(r->buf, data + first, len - first);

	r->head = (r->head + len) % r->size;
	if (r->head == r->tail) r->full = true;

	r->accepted += len;
	uint32_t used = logRingUsed(r);
	if (used > r->peakUsed) r->peakUsed = used;
	return true;
}

uint32_t logRingPeek(const LogRing *r, const uint8_t **out) {
	uint32_t used = logRingUsed(r);
	if (used == 0) return 0;

	*out = r->buf + r->tail;
	// Stop at the end of the array; a wrapped buffer takes two calls. Handing
	// back a contiguous run lets the SD layer write straight from the ring.
	uint32_t toEnd = r->size - r->tail;
	return used < toEnd ? used : toEnd;
}

void logRingConsume(LogRing *r, uint32_t n) {
	uint32_t used = logRingUsed(r);
	if (n > used) n = used;
	if (n == 0) return;

	r->tail = (r->tail + n) % r->size;
	r->full = false;
}
