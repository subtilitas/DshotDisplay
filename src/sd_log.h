/**
 * @file sd_log.h
 * @brief Blackbox logging to the microSD slot.
 *
 * Ties together @ref BlackboxEncoder (which turns telemetry into bytes),
 * @ref LogRing (which absorbs card stalls) and FatFs (which does the writing).
 *
 * @section sdlog_cores Which core
 *
 * Core0, always. Core1's only job is DShot frame timing, and an SD card that
 * pauses 30 ms for an internal erase would destroy it. Core0 already tolerates
 * multi-millisecond display DMA bursts and the UI is not real-time critical.
 *
 * The encoder therefore runs on core0 too, reading telemetry through
 * escSnapshot() like the UI does.
 *
 * @section sdlog_loss What happens when the card cannot keep up
 *
 * Bytes are dropped and counted; nothing ever blocks waiting for the card. A
 * gap in the log is recoverable, a stalled UI with a live motor is not. Drops
 * are surfaced rather than swallowed — a silently lossy log is worse than an
 * obviously lossy one, because it looks like data.
 *
 * @see docs/design/blackbox-logging.md
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** @brief What the logger is currently doing. */
enum class SdLogState : uint8_t {
	NoCard = 0,  /**< No card, or mount failed. Not an error; just unavailable. */
	Idle,        /**< Card present and ready. */
	Logging,     /**< A file is open and being written. */
	Error,       /**< A write or open failed. Card needs re-mounting. */
};

/** @brief Counters for the UI. Copied out by sdLogStatus(). */
struct SdLogStatus {
	SdLogState state;
	uint32_t   bytesWritten;  /**< Bytes handed to the card this session. */
	uint32_t   framesLogged;  /**< Frames encoded this session. */
	uint32_t   bytesDropped;  /**< Bytes the ring refused. */
	uint32_t   dropEvents;    /**< Frames lost to a full ring. */
	uint32_t   peakBuffer;    /**< Ring high-water mark, bytes. */
	uint32_t   worstFlushMs;  /**< Longest single card write. Sizes the buffer. */
	uint16_t   fileNumber;    /**< N in LOGnnnnn.BFL, 0 when not logging. */
};

/**
 * @brief Bring up the card. Safe to call when no card is fitted.
 *
 * @return True if a card mounted. False leaves the state at
 *         @ref SdLogState::NoCard and every other call a no-op.
 */
bool sdLogBegin();

/**
 * @brief Open the next free `LOGnnnnn.BFL` and start encoding.
 * @return True if the file opened.
 */
bool sdLogStart();

/** @brief Finish the current log: end event, flush, close. No-op when idle. */
void sdLogStop();

/** @brief True while a file is open. @return Logging state. */
bool sdLogActive();

/**
 * @brief Encode one frame if the log rate says it is due.
 *
 * Call from the core0 loop as often as convenient; it rate-limits internally to
 * @ref SD_LOG_RATE_HZ. Cheap and non-blocking — it only fills the ring.
 *
 * @param nowUs    Current micros().
 * @param throttle Commanded throttle, 0..2000. Passed in rather than read back
 *                 from the ESC task: the UI is what commands it, and the value
 *                 core1 last transmitted is not necessarily the one the
 *                 operator asked for (it zeroes on disarm and on heartbeat
 *                 loss). The log should record the command.
 */
void sdLogTick(uint32_t nowUs, uint16_t throttle);

/**
 * @brief Push buffered bytes at the card.
 *
 * Separate from sdLogTick() because this is the call that can block for
 * milliseconds. Keeping them apart means the caller decides when to take that
 * cost, rather than discovering it inside a function that looks cheap.
 *
 * Writes at most one @ref SD_LOG_CHUNK_BYTES chunk per call.
 */
void sdLogFlush();

/**
 * @brief Read the counters.
 * @param[out] out Filled in.
 */
void sdLogStatus(SdLogStatus *out);

/**
 * @brief Tell the logger the tester armed or disarmed.
 *
 * Starts and stops a log when @ref SD_LOG_AUTO_ON_ARM is set; records an
 * explicit start otherwise. Idempotent.
 *
 * @param armed New arm state.
 */
void sdLogSetArmed(bool armed);
