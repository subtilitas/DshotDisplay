#include "sd_log.h"
#include "config.h"

#include <string.h>

#if SD_LOG_ENABLE

#include "blackbox_encode.h"
#include "log_ring.h"
#include "esc_task.h"
#include "esc_merge.h"
#include "board_pins.h"

#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>

/**
 * @file sd_log.cpp
 * @brief SD card blackbox logging. Core0 only.
 */

static SdFs   s_sd;
static FsFile s_file;

static SdLogState s_state = SdLogState::NoCard;
static uint16_t   s_fileNo = 0;

static BlackboxEncoder s_enc;
static LogRing         s_ring;
static uint8_t         s_ringBuf[SD_LOG_BUFFER_BYTES];

static uint32_t s_nextFrameUs = 0;
static uint32_t s_worstFlushMs = 0;
static uint32_t s_framesLogged = 0;
static uint32_t s_cardBytes = 0;
static bool     s_armed = false;

/** @brief Microseconds between logged frames. */
static const uint32_t kFramePeriodUs = 1000000u / SD_LOG_RATE_HZ;

/**
 * @brief Encoder sink: hand bytes to the ring, never to the card.
 *
 * This runs inside bbWriteFrame(), so it must not block. Anything that talks to
 * the card belongs in sdLogFlush().
 */
static void ringSink(void *ctx, const uint8_t *data, size_t len) {
	logRingWrite((LogRing *)ctx, data, (uint32_t)len);
}

bool sdLogBegin() {
	// SPI1: the display is on SPI0, so the two never contend for a peripheral.
	SPI1.setRX(PIN_SD_MISO);
	SPI1.setTX(PIN_SD_MOSI);
	SPI1.setSCK(PIN_SD_SCK);
	SPI1.setCS(PIN_SD_CS);

	SdSpiConfig cfg(PIN_SD_CS, SHARED_SPI, SD_SCK_MHZ(SD_LOG_SPI_MHZ), &SPI1);
	if (!s_sd.begin(cfg)) {
		// No card is the normal case on a bench, not a fault. Logging is simply
		// unavailable and the UI says so.
		s_state = SdLogState::NoCard;
		return false;
	}

	logRingInit(&s_ring, s_ringBuf, sizeof(s_ringBuf));
	s_state = SdLogState::Idle;
	return true;
}

/**
 * @brief Find the lowest unused `LOGnnnnn.BFL`.
 *
 * Linear probe. Slow if there are thousands of logs, but it happens once per
 * file and it keeps numbering stable across power cycles, which matters more
 * than the milliseconds — a bench session is a sequence of numbered runs.
 */
static uint16_t nextFileNumber() {
	char name[16];
	for (uint16_t n = 1; n < 10000; n++) {
		snprintf(name, sizeof(name), "LOG%05u.BFL", (unsigned)n);
		if (!s_sd.exists(name)) return n;
	}
	return 0;
}

bool sdLogStart() {
	if (s_state == SdLogState::NoCard) return false;
	if (s_state == SdLogState::Logging) return true;

	uint16_t n = nextFileNumber();
	if (n == 0) { s_state = SdLogState::Error; return false; }

	char name[16];
	snprintf(name, sizeof(name), "LOG%05u.BFL", (unsigned)n);
	if (!s_file.open(name, O_WRONLY | O_CREAT | O_TRUNC)) {
		s_state = SdLogState::Error;
		return false;
	}

	// Reserve a contiguous run so the FAT is not updated mid-log. An allocation
	// landing in the middle of a run is the long unpredictable stall this whole
	// design exists to avoid. Failure is not fatal, just slower and riskier.
	s_file.preAllocate(SD_LOG_PREALLOC_BYTES);

	logRingReset(&s_ring);
	s_ring.dropped = s_ring.drops = s_ring.peakUsed = 0;

	BlackboxSink sink = { ringSink, &s_ring };
	bbBegin(&s_enc, &sink, SD_LOG_I_INTERVAL);

	s_fileNo = n;
	s_framesLogged = 0;
	s_cardBytes = 0;
	s_worstFlushMs = 0;
	s_nextFrameUs = micros();
	s_state = SdLogState::Logging;
	return true;
}

void sdLogStop() {
	if (s_state != SdLogState::Logging) return;

	bbEnd(&s_enc);

	// Drain what is left. Bounded so a failing card cannot hang the UI here;
	// whatever does not make it out is lost, which is what the counters are for.
	for (int i = 0; i < 64 && logRingUsed(&s_ring) > 0; i++) sdLogFlush();

	s_file.truncate();   // hand back the unused pre-allocation
	s_file.sync();
	s_file.close();

	s_fileNo = 0;
	s_state = SdLogState::Idle;
}

bool sdLogActive() { return s_state == SdLogState::Logging; }

void sdLogTick(uint32_t nowUs, uint16_t throttle) {
	if (s_state != SdLogState::Logging) return;
	if ((int32_t)(nowUs - s_nextFrameUs) < 0) return;

	s_nextFrameUs += kFramePeriodUs;
	// If we fell far behind -- a long card stall, a debugger pause -- resync
	// rather than burst out a hundred frames with bunched timestamps.
	if ((int32_t)(micros() - s_nextFrameUs) > (int32_t)(kFramePeriodUs * 4)) {
		s_nextFrameUs = micros() + kFramePeriodUs;
	}

	EscTelemetry t;
	escSnapshot(&t);

	EscReading r;
	escMerge(&t, millis(), KISS_STALE_MS, &r);

	int32_t v[BB_FIELD_COUNT] = {0};
	// Every field but temperature is declared unsigned in the log header, and a
	// negative there wraps to ~4.29e9 rather than failing. Clamping is cheaper
	// than a log that decodes to nonsense. @see bbWriteFrame()
	v[BB_F_THROTTLE]     = throttle > 2000 ? 2000 : throttle;
	v[BB_F_ERPM]         = (int32_t)t.erpm;
	v[BB_F_ERPM_KISS]    = (int32_t)r.kissErpm;
	v[BB_F_VBAT]         = (int32_t)(r.voltsFrom == EscSource::Kiss ? t.kissVolts * 100.0f + 0.5f : 0.0f);
	v[BB_F_AMPERAGE]     = (int32_t)(r.ampsFrom == EscSource::Kiss ? t.kissAmps * 100.0f + 0.5f : 0.0f);
	v[BB_F_VBAT_EDT]     = (int32_t)(t.haveVolts ? t.volts * 100.0f + 0.5f : 0.0f);
	v[BB_F_AMPERAGE_EDT] = (int32_t)(t.haveAmps ? t.amps * 100.0f + 0.5f : 0.0f);
	v[BB_F_TEMP]         = r.tempC;                    // the one signed field
	v[BB_F_MAH]          = r.mah;
	v[BB_F_STRESS]       = t.stress;

	for (int i = 0; i < BB_FIELD_COUNT; i++) {
		if (i != BB_F_TEMP && v[i] < 0) v[i] = 0;
	}

	bbWriteFrame(&s_enc, nowUs, v);
	s_framesLogged++;
}

void sdLogFlush() {
	if (s_state != SdLogState::Logging) return;

	const uint8_t *p = nullptr;
	uint32_t avail = logRingPeek(&s_ring, &p);
	if (avail == 0) return;

	// Sector-aligned chunks, so the card is not doing read-modify-write on our
	// behalf. Below a full chunk, wait -- unless the log is stopping, in which
	// case sdLogStop() drains whatever is left regardless.
	uint32_t n = avail < SD_LOG_CHUNK_BYTES ? avail : SD_LOG_CHUNK_BYTES;

	uint32_t t0 = millis();
	size_t wrote = s_file.write(p, n);
	uint32_t took = millis() - t0;
	if (took > s_worstFlushMs) s_worstFlushMs = took;

	if (wrote != n) {
		s_state = SdLogState::Error;
		s_file.close();
		return;
	}

	logRingConsume(&s_ring, n);
	s_cardBytes += n;
}

void sdLogStatus(SdLogStatus *out) {
	out->state        = s_state;
	out->bytesWritten = s_cardBytes;
	out->framesLogged = s_framesLogged;
	out->bytesDropped = s_ring.dropped;
	out->dropEvents   = s_ring.drops;
	out->peakBuffer   = s_ring.peakUsed;
	out->worstFlushMs = s_worstFlushMs;
	out->fileNumber   = s_fileNo;
}

void sdLogSetArmed(bool armed) {
	if (armed == s_armed) return;
	s_armed = armed;

#if SD_LOG_AUTO_ON_ARM
	if (armed) sdLogStart();
	else       sdLogStop();
#endif
}

#else  // !SD_LOG_ENABLE

// Stubs, so callers need no #if of their own. The linker drops them.
bool sdLogBegin() { return false; }
bool sdLogStart() { return false; }
void sdLogStop() {}
bool sdLogActive() { return false; }
void sdLogTick(uint32_t, uint16_t) {}
void sdLogFlush() {}
void sdLogSetArmed(bool) {}
void sdLogStatus(SdLogStatus *out) {
	memset(out, 0, sizeof(*out));
	out->state = SdLogState::NoCard;
}

#endif
