/**
 * @file log_ring.h
 * @brief Byte ring buffer between the log encoder and the SD card.
 *
 * The problem this exists to solve: an SD card can stall for tens of
 * milliseconds while it does an internal erase, and there is no way to know in
 * advance which write will be the slow one. Encoding straight to the card would
 * mean the UI freezing for 30 ms at unpredictable moments, with a motor
 * spinning.
 *
 * So the encoder writes here, which is always fast, and the card is drained
 * separately in whatever time is available.
 *
 * Two decisions worth stating, because both are the opposite of what a general
 * ring buffer would do:
 *
 * - **Writes are all-or-nothing.** A frame that does not fit whole is dropped
 *   whole. Accepting half a frame would splice two frames together in the byte
 *   stream and the decoder would resynchronise somewhere arbitrary — a
 *   corrupted log that still parses is worse than a log with an honest gap.
 * - **Overrun drops, it never blocks.** A gap in the log is recoverable; a
 *   stalled UI with a live motor is not.
 *
 * Pure: no hardware, no Arduino, host-testable.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Ring buffer state.
 *
 * Storage is caller-supplied so the buffer can live in a static array sized by
 * a config constant rather than on a heap the firmware would rather not have.
 */
struct LogRing {
	uint8_t *buf;       /**< Caller's storage. */
	uint32_t size;      /**< Bytes in @ref buf. */
	uint32_t head;      /**< Next write offset. */
	uint32_t tail;      /**< Next read offset. */
	bool     full;      /**< Distinguishes full from empty when head == tail. */

	uint32_t accepted;  /**< Bytes taken in since init. */
	uint32_t dropped;   /**< Bytes refused for want of room. */
	uint32_t drops;     /**< Write calls refused. Frames lost, roughly. */
	uint32_t peakUsed;  /**< High-water mark, for sizing the buffer honestly. */
};

/**
 * @brief Attach storage and reset.
 * @param r    Ring.
 * @param buf  Storage. Must outlive the ring.
 * @param size Bytes available. Zero makes every write fail cleanly.
 */
void logRingInit(LogRing *r, uint8_t *buf, uint32_t size);

/** @brief Discard contents; keep the counters. @param r Ring. */
void logRingReset(LogRing *r);

/** @brief Bytes currently buffered. @param r Ring. @return Count. */
uint32_t logRingUsed(const LogRing *r);

/** @brief Bytes that would fit right now. @param r Ring. @return Count. */
uint32_t logRingFree(const LogRing *r);

/**
 * @brief Append bytes, all or nothing.
 *
 * @param r    Ring.
 * @param data Bytes.
 * @param len  Count. Zero succeeds trivially.
 * @return     True if accepted. False means nothing was written and
 *             @ref LogRing::drops was incremented.
 */
bool logRingWrite(LogRing *r, const uint8_t *data, uint32_t len);

/**
 * @brief Look at the longest run of buffered bytes without copying.
 *
 * Returns only up to the end of the storage array, so a wrapped buffer needs
 * two calls to drain. That is deliberate: it lets the SD layer hand a pointer
 * straight to the card without an intermediate copy.
 *
 * @param r        Ring.
 * @param[out] out Set to the first readable byte. Untouched when empty.
 * @return         Bytes readable from @p out, possibly zero.
 */
uint32_t logRingPeek(const LogRing *r, const uint8_t **out);

/**
 * @brief Drop the oldest @p n bytes, after they have been written out.
 * @param r Ring.
 * @param n Bytes to release. Clamped to what is buffered.
 */
void logRingConsume(LogRing *r, uint32_t n);
