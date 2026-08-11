#include "kiss_telem.h"

#include <string.h>

/**
 * @file kiss_telem.cpp
 * @brief KISS telemetry frame decode. No hardware, no Arduino, host-testable.
 */

/**
 * @brief One CRC8 round.
 *
 * Written in Betaflight's shape rather than a table so the two can be compared
 * by eye. A 256-byte table would be faster, but this runs ten times per frame
 * at 50 Hz — five thousand iterations a second — and the clarity is worth more
 * than the cycles.
 */
static inline uint8_t crc8Round(uint8_t data, uint8_t seed) {
	uint8_t crc = (uint8_t)(data ^ seed);
	for (int i = 0; i < 8; i++) {
		crc = (crc & 0x80) ? (uint8_t)(0x07 ^ (uint8_t)(crc << 1))
		                   : (uint8_t)(crc << 1);
	}
	return crc;
}

uint8_t kissCrc8(const uint8_t *buf, uint8_t len) {
	uint8_t crc = 0;
	for (uint8_t i = 0; i < len; i++) crc = crc8Round(buf[i], crc);
	return crc;
}

/** @brief Assemble a big-endian u16. */
static inline uint16_t be16(const uint8_t *p) {
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

bool kissDecodeFrame(const uint8_t *buf, KissFrame *out) {
	if (kissCrc8(buf, KISS_FRAME_LEN - 1) != buf[KISS_FRAME_LEN - 1]) return false;

	out->tempC = (int16_t)buf[0];
	out->volts = be16(&buf[1]) * 0.01f;
	out->amps  = be16(&buf[3]) * 0.01f;
	out->mah   = be16(&buf[5]);
	out->erpm  = (uint32_t)be16(&buf[7]) * 100u;
	return true;
}

void kissExpectFrame(KissDecoder *d) {
	d->have = 0;
}

bool kissFeed(KissDecoder *d, uint8_t b, KissFrame *out) {
	// Bytes arriving with no frame in flight are the ESC's startup chatter, or
	// the tail of a frame we gave up on. Count them rather than letting them
	// silently shift a stale buffer into alignment.
	if (d->have >= KISS_FRAME_LEN) {
		d->overrun++;
		return false;
	}

	d->buf[d->have++] = b;
	if (d->have < KISS_FRAME_LEN) return false;

	bool ok = kissDecodeFrame(d->buf, out);
	if (ok) d->good++;
	else    d->bad++;

	// Empty either way. Keeping a failed frame around to retry against the next
	// byte would only find a checksum that happened to match at the wrong
	// offset, which is worse than dropping it.
	d->have = 0;
	return ok;
}

uint16_t kissBuildDshotPayload(uint16_t throttle0to2000, bool requestTelemetry) {
	if (throttle0to2000 > 2000) throttle0to2000 = 2000;

	// Mirrors BidirDShotX1::sendThrottle(): the 47-command offset applies only
	// to non-zero throttle, so that zero stays the motor-stop command.
	uint16_t raw = throttle0to2000 ? (uint16_t)(throttle0to2000 + 47) : 0;

	return (uint16_t)((raw << 1) | (requestTelemetry ? 1u : 0u));
}
