/**
 * @file bridge_log.cpp
 * @brief Ring buffer and hex dump for USB-bridge traffic.
 */

#include "bridge_log.h"
#include "config.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#if AM32_BRIDGE_LOG

/** @brief Longest single transfer kept; anything longer is truncated. */
#define BLOG_MAX_PAYLOAD 64

/**
 * @brief Record header: 4 timestamp, 1 direction, 1 stored length, 1 true
 *        length.
 *
 * The true length is kept separately because payloads are capped. Printing
 * only the stored length claimed an 83-byte reply was 64 bytes, which during
 * debugging is worse than printing nothing.
 */
#define BLOG_HDR 7

/** @brief Give up on one line after this long, so a dead host cannot hang us. */
#define BLOG_EMIT_TMO_MS 400

static uint8_t  s_ring[AM32_BRIDGE_LOG_BYTES];
static uint16_t s_head = 0;      /**< Write cursor. */
static uint16_t s_tail = 0;      /**< Oldest record. */
static uint16_t s_used = 0;      /**< Bytes currently held. */
static uint16_t s_count = 0;     /**< Records currently held. */
static bool     s_overflow = false;
static uint32_t s_t0 = 0;        /**< Timestamps are relative to this. */

static inline void put(uint8_t b) {
	s_ring[s_head] = b;
	s_head = (uint16_t)((s_head + 1) % sizeof(s_ring));
	s_used++;
}

static inline uint8_t at(uint16_t off) {
	return s_ring[(s_tail + off) % sizeof(s_ring)];
}

/** @brief Drop the oldest record to free space. */
static void dropOldest() {
	if (!s_count) return;
	uint16_t len = at(5);
	uint16_t total = (uint16_t)(BLOG_HDR + len);
	s_tail = (uint16_t)((s_tail + total) % sizeof(s_ring));
	s_used = (uint16_t)(s_used - total);
	s_count--;
	s_overflow = true;
}

void bridgeLogReset() {
	s_head = s_tail = s_used = s_count = 0;
	s_overflow = false;
	s_t0 = millis();
}

void bridgeLogAdd(BridgeLogDir dir, const uint8_t *data, uint16_t len) {
	uint16_t trueLen = len;
	if (len > BLOG_MAX_PAYLOAD) len = BLOG_MAX_PAYLOAD;
	uint16_t need = (uint16_t)(BLOG_HDR + len);
	if (need > sizeof(s_ring)) return;

	// Keeping the newest traffic is the right trade: a session that fails does
	// so at the end, and the first frames are usually the ones that worked.
	while (sizeof(s_ring) - s_used < need) dropOldest();

	uint32_t ms = millis() - s_t0;
	put((uint8_t)(ms));
	put((uint8_t)(ms >> 8));
	put((uint8_t)(ms >> 16));
	put((uint8_t)(ms >> 24));
	put((uint8_t)dir);
	put((uint8_t)len);
	put((uint8_t)(trueLen > 255 ? 255 : trueLen));
	for (uint16_t i = 0; i < len; i++) put(data[i]);
	s_count++;
}

void bridgeLogNote(const char *text) {
	bridgeLogAdd(BLOG_NOTE, (const uint8_t *)text, (uint16_t)strlen(text));
}

uint16_t bridgeLogCount() { return s_count; }
bool bridgeLogOverflowed() { return s_overflow; }

/**
 * @brief Emit one line, then wait for USB to drain it.
 *
 * The dump used to be a burst of small printf calls. USB CDC on this core has
 * a modest TX buffer and *drops* writes once it is full rather than blocking,
 * so a 14-record dump silently printed its first six lines and stopped — while
 * the header still claimed 14. Composing whole lines and flushing each one
 * keeps the buffer from ever filling.
 */
static void emit(const char *line) {
	size_t len = strlen(line);
	size_t sent = 0;
	// Bounded so a host that stops reading cannot hang the UI. Losing the tail
	// of a dump is bad; a board that never comes back is worse.
	uint32_t deadline = millis() + BLOG_EMIT_TMO_MS;

	while (sent < len) {
		if ((int32_t)(millis() - deadline) >= 0) return;

		// Ask how much room there is rather than writing and hoping. This is
		// the whole fix: Serial.write() on USB CDC returns having queued
		// nothing once its buffer is full, so writing a burst and pausing
		// afterwards still loses everything past the first bufferful.
		int room = Serial.availableForWrite();
		if (room <= 0) {
			delay(1);
			continue;
		}
		size_t chunk = len - sent;
		if (chunk > (size_t)room) chunk = (size_t)room;
		size_t wrote = Serial.write((const uint8_t *)line + sent, chunk);
		if (!wrote) { delay(1); continue; }
		sent += wrote;
	}
	Serial.flush();
}

void bridgeLogDump() {
	char line[BLOG_MAX_PAYLOAD * 3 + 64];

	snprintf(line, sizeof(line), "\n--- AM32 BRIDGE LOG: %u record(s)%s ---\n",
	         (unsigned)s_count, s_overflow ? ", OLDEST DROPPED" : "");
	emit(line);
	emit("  ms  dir      n  bytes\n");

	uint16_t off = 0;
	uint32_t prev = 0;
	for (uint16_t r = 0; r < s_count; r++) {
		uint32_t ms = (uint32_t)at(off) | ((uint32_t)at(off + 1) << 8) |
		              ((uint32_t)at(off + 2) << 16) | ((uint32_t)at(off + 3) << 24);
		uint8_t dir = at(off + 4);
		uint8_t len = at(off + 5);        // bytes kept
		uint8_t total = at(off + 6);      // bytes actually transferred
		off += BLOG_HDR;

		if (dir == BLOG_NOTE) {
			char note[BLOG_MAX_PAYLOAD + 1];
			for (uint8_t i = 0; i < len; i++) note[i] = (char)at(off + i);
			note[len] = '\0';
			snprintf(line, sizeof(line), "%6lu  ----     -  %s\n",
			         (unsigned long)ms, note);
		} else {
			// The gap since the previous record is the number that matters:
			// echo and reply must reach the host inside one of its polls.
			int p = snprintf(line, sizeof(line), "%6lu  %s %3u  ",
			                 (unsigned long)ms,
			                 dir == BLOG_HOST_TO_ESC ? "H>E " : "E>H ", total);
			for (uint8_t i = 0; i < len && p < (int)sizeof(line) - 4; i++) {
				p += snprintf(line + p, sizeof(line) - p, "%02X ", at(off + i));
			}
			if (total > len && p < (int)sizeof(line) - 24) {
				p += snprintf(line + p, sizeof(line) - p, "...(%u of %u shown)",
				              (unsigned)len, (unsigned)total);
			}
			if (r && p < (int)sizeof(line) - 16) {
				p += snprintf(line + p, sizeof(line) - p, "  (+%lums)",
				              (unsigned long)(ms - prev));
			}
			snprintf(line + p, sizeof(line) - p, "\n");
		}
		emit(line);
		prev = ms;
		off += len;
	}
	emit("--- END ---\n\n");
}

#else   // AM32_BRIDGE_LOG disabled: keep the API, cost nothing

void bridgeLogReset() {}
void bridgeLogAdd(BridgeLogDir, const uint8_t *, uint16_t) {}
void bridgeLogNote(const char *) {}
void bridgeLogDump() {}
uint16_t bridgeLogCount() { return 0; }
bool bridgeLogOverflowed() { return false; }

#endif
