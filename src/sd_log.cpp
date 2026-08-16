#include "sd_log.h"
#include "config.h"

#include <string.h>

#if SD_LOG_ENABLE

#include "blackbox_encode.h"
#include "log_ring.h"
#include "esc_task.h"
#include "esc_merge.h"
#include "board_desc.h"

#include "plat.h"
#include "ff.h"
#include "f_util.h"
#include "hw_config.h"
#include "sd_card.h"

#include <stdio.h>

/**
 * @file sd_log.cpp
 * @brief SD card blackbox logging. Core0 only.
 */

static FATFS s_fs;
static FIL   s_file;
static bool  s_mounted = false;
/**
 * @brief True while the card has been handed to a USB host.
 *
 * Distinct from `!s_mounted`, and the distinction is the point: unmounted is a
 * state this file gets itself out of, by remounting, at any of half a dozen
 * call sites. Released is a state only sdLogReacquire() may leave, because
 * something outside this file is reading the card and a helpful remount would
 * corrupt it. See usb_msc.h.
 */
static bool  s_released = false;
static uint8_t  s_mountResult = 0;
static uint8_t  s_cardType = 0;
static uint32_t s_cardSectors = 0;
static uint32_t s_cardSizeMB = 0;

static SdLogState s_state = SdLogState::NoCard;
static uint16_t   s_fileNo = 0;

static BlackboxEncoder s_enc;
static LogRing         s_ring;
static uint8_t         s_ringBuf[SD_LOG_BUFFER_BYTES];

static uint32_t s_nextFrameUs = 0;
static uint32_t s_worstFlushMs = 0;
/** @brief millis() of the last f_sync(). @see SD_LOG_SYNC_MS */
static uint32_t s_lastSyncMs = 0;
/** @brief Set while sdLogStop() drains, so the last partial chunk goes out. */
static bool     s_draining = false;
static uint32_t s_framesLogged = 0;
static uint32_t s_cardBytes = 0;
static bool     s_armed = false;
/**
 * @brief True when the log in progress was begun by arming, not by the button.
 *
 * Auto-stop must only undo what auto-start did. Entering the settings screen
 * force-disarms -- and the logging screen is reached through it -- so without
 * this, walking back to check on a log you started by hand is what stops it.
 */
static bool     s_autoStarted = false;

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

/**
 * @brief The FatFs drive prefix for our one card.
 *
 * Asked of the library rather than hardcoded as "0:". The prefix depends on how
 * FatFs was configured -- numeric or string volume IDs, how many volumes -- and
 * guessing it wrong fails as FR_INVALID_DRIVE, which looks identical to a
 * missing card from the outside.
 */
static const char *drivePrefix() {
	sd_card_t *card = sd_get_by_num(0);
	return card ? sd_get_drive_prefix(card) : "";
}

bool sdLogBegin() {
	// Push this board's SD wiring into the driver's structs first. They are
	// filled at run time now, because the two boards use different interfaces
	// entirely and a unified image carries both.
	sdHwConfigApply();

	// Pins come from sd_hw_config.c, which the driver reads at link time.
	// Mounting is the only way to find out whether a card is fitted: this board
	// brings no card-detect line out.
	FRESULT fr = f_mount(&s_fs, drivePrefix(), 1);
	s_mountResult = (uint8_t)fr;

	// Whatever the mount did, the driver has by now tried to talk to the card,
	// so its type and size say whether anything answered at all. That separates
	// "nothing on the bus" from "card works, filesystem unreadable" -- the two
	// look the same from the outside and want completely different fixes.
	sd_card_t *card = sd_get_by_num(0);
	if (card) {
		s_cardType = (uint8_t)card->state.card_type;
		// The SDIO driver never fills card_type in -- it is an SPI-mode concept,
		// set from the CMD58 response. Sector count is the interface-independent
		// signal that something answered, so presence is judged on that.
		// Reading card_type alone showed "NONE" for a card that had just
		// mounted and written a file.
		s_cardSectors = card->state.sectors;
		if (!s_cardSectors && card->get_num_sectors)
			s_cardSectors = card->get_num_sectors(card);
		s_cardSizeMB = s_cardSectors / 2048u;
	}

	if (fr != FR_OK) {
		// No card is the normal case on a bench, not a fault. Logging is simply
		// unavailable and the UI says so.
		s_mounted = false;
		s_state = SdLogState::NoCard;
		return false;
	}

	s_mounted = true;
	logRingInit(&s_ring, s_ringBuf, sizeof(s_ringBuf));
	s_state = SdLogState::Idle;
	return true;
}

bool sdLogRemount() {
	if (s_released) return false;
	if (s_state == SdLogState::Logging) return true;
	if (s_mounted) f_unmount(drivePrefix());
	s_mounted = false;
	return sdLogBegin();
}

bool sdLogRelease() {
	// Refuse rather than tear down a recording. The caller is expected to have
	// asked mscRefusal() first and to have been told no; this is the backstop
	// for a caller that did not, and losing a run in progress to a mis-tap is
	// exactly what it exists to prevent.
	if (s_state == SdLogState::Logging) return false;

	if (s_mounted) {
		// FatFs caches. Unmounting is what makes the on-card filesystem match
		// what the host is about to read -- without it the host sees a FAT that
		// is missing whatever is still sitting in s_fs.
		f_unmount(drivePrefix());
		s_mounted = false;
	}
	// Not SdLogState::Error: nothing failed. From the logger's point of view
	// there is no card, which is exactly true while somebody else has it, and
	// it is the state every other entry point already refuses to work in.
	s_state = SdLogState::NoCard;
	s_released = true;
	return true;
}

bool sdLogReacquire() {
	if (!s_released) return s_mounted;
	s_released = false;
	// A full sdLogBegin(), not just an f_mount: the host may have been given a
	// card that was then pulled out, and re-reading type and size is how the
	// screen tells that apart from a card that is simply back.
	return sdLogBegin();
}

bool sdLogReleased() { return s_released; }

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
		FILINFO fno;
		if (f_stat(name, &fno) == FR_NO_FILE) return n;
	}
	return 0;
}

bool sdLogStart() {
	if (s_released) return false;
	if (s_state == SdLogState::NoCard) return false;
	if (s_state == SdLogState::Logging) return true;

	uint16_t n = nextFileNumber();
	if (n == 0) { s_state = SdLogState::Error; return false; }

	char name[16];
	snprintf(name, sizeof(name), "LOG%05u.BFL", (unsigned)n);
	if (f_open(&s_file, name, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
		s_state = SdLogState::Error;
		return false;
	}

	// Reserve a contiguous run so the FAT is not extended mid-log. An
	// allocation landing in the middle of a run is the long unpredictable stall
	// this whole design exists to avoid. FA_WRITE|f_expand with opt=1 allocates
	// immediately rather than lazily. Failure is not fatal, just slower.
	f_expand(&s_file, SD_LOG_PREALLOC_BYTES, 1);

	logRingReset(&s_ring);
	s_ring.dropped = s_ring.drops = s_ring.peakUsed = 0;

	BlackboxSink sink = { ringSink, &s_ring };
	bbBegin(&s_enc, &sink, SD_LOG_I_INTERVAL);

	s_autoStarted = false;   // callers that want auto set it after this returns
	s_fileNo = n;
	s_framesLogged = 0;
	s_cardBytes = 0;
	s_worstFlushMs = 0;
	s_nextFrameUs = micros();
	s_lastSyncMs = millis();
	s_draining = false;
	s_state = SdLogState::Logging;
	return true;
}

void sdLogStop() {
	if (s_state != SdLogState::Logging) return;

	bbEnd(&s_enc);

	// Drain what is left. Bounded so a failing card cannot hang the UI here;
	// whatever does not make it out is lost, which is what the counters are for.
	// s_draining lets sdLogFlush() write a partial chunk, which it otherwise
	// refuses to do -- without it the tail of every log would be discarded.
	s_draining = true;
	for (int i = 0; i < 64 && logRingUsed(&s_ring) > 0; i++) sdLogFlush();
	s_draining = false;

	// Hand back the unused pre-allocation, then commit the directory entry.
	f_truncate(&s_file);
	f_sync(&s_file);
	f_close(&s_file);

	s_fileNo = 0;
	s_autoStarted = false;
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
	uint32_t nowMs = millis();
	escMerge(&t, nowMs, KISS_STALE_MS, EDT_STALE_MS, &r);

	int32_t v[BB_FIELD_COUNT] = {0};
	// Every field but temperature is declared unsigned in the log header, and a
	// negative there wraps to ~4.29e9 rather than failing. Clamping is cheaper
	// than a log that decodes to nonsense. @see bbWriteFrame()
	v[BB_F_THROTTLE]     = throttle > 2000 ? 2000 : throttle;
	v[BB_F_ERPM]         = (int32_t)t.erpm;
	v[BB_F_ERPM_KISS]    = (int32_t)r.kissErpm;
	v[BB_F_VBAT]         = (int32_t)(r.voltsFrom == EscSource::Kiss ? t.kissVolts * 100.0f + 0.5f : 0.0f);
	v[BB_F_AMPERAGE]     = (int32_t)(r.ampsFrom == EscSource::Kiss ? t.kissAmps * 100.0f + 0.5f : 0.0f);
	// The EDT columns are logged raw rather than merged, so they need the same
	// expiry applied by hand -- otherwise a disconnected ESC writes its last
	// voltage into every subsequent frame and the trace shows a rock-steady
	// pack instead of the moment the data stopped.
	v[BB_F_VBAT_EDT]     = (int32_t)(escFieldFresh(t.edtVoltsMs, nowMs, EDT_STALE_MS)
	                                 ? t.volts * 100.0f + 0.5f : 0.0f);
	v[BB_F_AMPERAGE_EDT] = (int32_t)(escFieldFresh(t.edtAmpsMs, nowMs, EDT_STALE_MS)
	                                 ? t.amps * 100.0f + 0.5f : 0.0f);
	v[BB_F_TEMP]         = r.tempC;                    // the one signed field
	v[BB_F_MAH]          = r.mah;
	v[BB_F_STRESS]       = r.stress;

	for (int i = 0; i < BB_FIELD_COUNT; i++) {
		if (i != BB_F_TEMP && v[i] < 0) v[i] = 0;
	}

	bbWriteFrame(&s_enc, nowUs, v);
	s_framesLogged++;
}

/**
 * @brief Commit the directory entry every @ref SD_LOG_SYNC_MS, at most.
 *
 * Without this the only f_sync() is in sdLogStop(), so a log that ends by the
 * battery being pulled has no directory entry at all and the whole run is lost.
 * That is not a hypothetical ending: "have a way to cut battery power that is
 * not the touchscreen" is the safety advice this firmware ships with.
 *
 * The cost is one FAT update against an already-preallocated contiguous run, so
 * the stall is small and bounded -- and it is taken while the buffer is empty,
 * immediately after a chunk went out, which is the cheapest moment there is.
 */
static void syncIfDue() {
	if (s_state != SdLogState::Logging || s_draining) return;
	uint32_t now = millis();
	if ((uint32_t)(now - s_lastSyncMs) < SD_LOG_SYNC_MS) return;
	s_lastSyncMs = now;
	uint32_t t0 = now;
	f_sync(&s_file);
	uint32_t took = millis() - t0;
	if (took > s_worstFlushMs) s_worstFlushMs = took;
}

void sdLogFlush() {
	if (s_state != SdLogState::Logging) return;

	const uint8_t *p = nullptr;
	uint32_t avail = logRingPeek(&s_ring, &p);

	// Sector-aligned chunks, so the card is not doing read-modify-write on our
	// behalf. Actually waiting for one, which this did not used to do: it wrote
	// whatever was available, and since loop() calls this on every pass with no
	// rate limit, "whatever was available" was almost always a few bytes. Every
	// write was then a partial sector -- precisely the read-modify-write the
	// chunking exists to avoid, and a large part of what WORST FLUSH measured.
	if (avail == 0 || (avail < SD_LOG_CHUNK_BYTES && !s_draining)) {
		syncIfDue();
		return;
	}

	uint32_t n = avail < SD_LOG_CHUNK_BYTES ? avail : SD_LOG_CHUNK_BYTES;

	uint32_t t0 = millis();
	UINT wrote = 0;
	FRESULT fr = f_write(&s_file, p, n, &wrote);
	uint32_t took = millis() - t0;
	if (took > s_worstFlushMs) s_worstFlushMs = took;

	if (fr != FR_OK || wrote != n) {
		s_state = SdLogState::Error;
		f_close(&s_file);
		return;
	}

	logRingConsume(&s_ring, n);
	s_cardBytes += n;
	syncIfDue();
}

void sdLogStatus(SdLogStatus *out) {
	out->state        = s_state;
	out->mountResult  = s_mountResult;
	out->cardType     = s_cardType;
	out->cardSizeMB   = s_cardSizeMB;
	out->bytesWritten = s_cardBytes;
	out->framesLogged = s_framesLogged;
	out->bytesDropped = s_ring.dropped;
	out->dropEvents   = s_ring.drops;
	out->peakBuffer   = s_ring.peakUsed;
	out->worstFlushMs = s_worstFlushMs;
	out->fileNumber   = s_fileNo;
}

void sdLogSetArmed(bool armed) {
	SdLogArmAction act = sdLogArmAction(armed, s_armed, sdLogActive(),
	                                    s_autoStarted);
	s_armed = armed;

	switch (act) {
		case SdLogArmAction::Start: s_autoStarted = sdLogStart(); break;
		case SdLogArmAction::Stop:  sdLogStop();                  break;
		case SdLogArmAction::None:                                break;
	}
}

#else  // !SD_LOG_ENABLE

// Stubs, so callers need no #if of their own. The linker drops them.
bool sdLogBegin() { return false; }
bool sdLogStart() { return false; }
void sdLogStop() {}
bool sdLogRemount() { return false; }
bool sdLogRelease() { return false; }
bool sdLogReacquire() { return false; }
bool sdLogReleased() { return false; }
bool sdLogActive() { return false; }
void sdLogTick(uint32_t, uint16_t) {}
void sdLogFlush() {}
void sdLogSetArmed(bool) {}
void sdLogStatus(SdLogStatus *out) {
	memset(out, 0, sizeof(*out));
	out->state = SdLogState::NoCard;
}

#endif
