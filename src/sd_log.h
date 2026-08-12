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

#include "config.h"

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
	/**
	 * @brief Last mount result, as a FatFs `FRESULT`. Zero is FR_OK.
	 *
	 * Surfaced because "not detected" covers several very different faults and
	 * collapsing them all to "no card" makes the problem unsolvable from the
	 * bench: 3 (FR_NOT_READY) means nothing answered on the bus, 13
	 * (FR_NO_FILESYSTEM) means the card is talking fine but has no partition
	 * FatFs recognises, and those want opposite fixes.
	 */
	uint8_t    mountResult;
	uint8_t    cardType;      /**< card_type_t: 0 none, 1 v1, 2 v2, 3 v2 HC/XC. */
	uint32_t   cardSizeMB;    /**< Capacity, 0 if the card never initialised. */
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

/**
 * @brief Try to mount the card again.
 *
 * sdLogBegin() runs once at boot, so a card inserted afterwards was previously
 * invisible until a power cycle — which is its own way of looking like a card
 * that "is not detected".
 *
 * @return True if a card mounted.
 */
bool sdLogRemount();

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

/** @brief What a change in arm state should do to the log. */
enum class SdLogArmAction : uint8_t {
	None,   /**< Leave the log alone. */
	Start,  /**< Begin an auto log. */
	Stop,   /**< End the auto log this arming began. */
};

/**
 * @brief Decide what an arm-state change should do. Pure; host-testable.
 *
 * Split out from sdLogSetArmed() so the host tests exercise the shipped rule
 * rather than a copy of it — sd_log.cpp pulls in FatFs and cannot be linked
 * into the test binary, and a fake that reimplements the policy proves only
 * that the fake agrees with itself.
 *
 * The rule that matters: auto-stop undoes only what auto-start did. Entering
 * the settings screen force-disarms, and the logging screen is reached through
 * it, so without that distinction walking over to check on a hand-started log
 * is what ends it.
 *
 * @param nowArmed    The new arm state.
 * @param wasArmed    The previous arm state.
 * @param logging     Whether a log is currently open.
 * @param autoStarted Whether the open log was begun by arming.
 * @return            What to do.
 */
static inline SdLogArmAction sdLogArmAction(bool nowArmed, bool wasArmed,
                                            bool logging, bool autoStarted) {
	if (nowArmed == wasArmed) return SdLogArmAction::None;
#if SD_LOG_AUTO_ON_ARM
	if (nowArmed) return logging ? SdLogArmAction::None : SdLogArmAction::Start;
	return autoStarted ? SdLogArmAction::Stop : SdLogArmAction::None;
#else
	(void)logging; (void)autoStarted;
	return SdLogArmAction::None;
#endif
}

/**
 * @brief Tell the logger the tester armed or disarmed.
 *
 * Starts and stops a log when @ref SD_LOG_AUTO_ON_ARM is set; records an
 * explicit start otherwise. Idempotent.
 *
 * @param armed New arm state.
 */
void sdLogSetArmed(bool armed);
