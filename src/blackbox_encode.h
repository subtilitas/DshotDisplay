/**
 * @file blackbox_encode.h
 * @brief Betaflight blackbox log writer.
 *
 * Emits the binary format that `blackbox_decode`, Blackbox Explorer and
 * PIDtoolbox already read. A CSV would be far less work; the reason not to is
 * that a log which opens in Blackbox Explorer can be scrubbed and overlaid
 * against flight logs from the same ESC on a real craft, which is exactly the
 * comparison a bench tester exists to make.
 *
 * Structure of a log:
 *
 * - An ASCII header of `H <name>:<value>\n` lines, opening with the exact
 *   product marker the parser scans for.
 * - `I` frames, carrying every field absolutely. The keyframe.
 * - `P` frames, carrying every field as a delta against predicted values.
 * - A final `E` frame with @ref BB_EVENT_LOG_END.
 *
 * Field 0 must be `loopIteration` and field 1 must be `time`; the decoder
 * hardcodes those indices. Everything after them is ours to choose.
 *
 * No hardware, no Arduino, no filesystem: bytes go to a @ref BlackboxSink the
 * caller supplies. The host tests point that at a file and run the real
 * `blackbox_decode` over the result.
 *
 * @see docs/design/blackbox-logging.md
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Field count. Two mandatory, then the ESC quantities. */
#define BB_FIELD_COUNT 12

/**
 * @defgroup bb_fields Field indices
 * @brief Positions within a frame. Order must match @ref bbFieldNames.
 * @{
 */
#define BB_F_ITERATION   0  /**< `loopIteration`. Mandatory at index 0. */
#define BB_F_TIME        1  /**< `time`, microseconds. Mandatory at index 1. */
#define BB_F_THROTTLE    2  /**< Commanded throttle, 0..2000. */
#define BB_F_ERPM        3  /**< eRPM from bidirectional DShot. */
#define BB_F_ERPM_KISS   4  /**< eRPM as KISS reported it. */
#define BB_F_VBAT        5  /**< Voltage from KISS, 0.01 V. */
#define BB_F_AMPERAGE    6  /**< Current from KISS, 0.01 A. */
#define BB_F_VBAT_EDT    7  /**< Voltage from EDT, 0.01 V. */
#define BB_F_AMPERAGE_EDT 8 /**< Current from EDT, 0.01 A. */
#define BB_F_TEMP        9  /**< ESC temperature, degrees Celsius. */
#define BB_F_MAH        10  /**< Consumption, mAh. KISS only. */
#define BB_F_STRESS     11  /**< EDT stress, 0..255. */
/** @} */

/** @brief Event IDs. Only the ones this firmware emits. */
#define BB_EVENT_LOG_END 255

/**
 * @brief Where encoded bytes go.
 *
 * Deliberately just a function pointer and a context: on device this lands in
 * the SD ring buffer, on the host it is a `FILE *`. The encoder never learns
 * which, so it stays testable.
 */
struct BlackboxSink {
	/**
	 * @brief Accept bytes. May drop them; the encoder does not check.
	 *
	 * Arguments are the context below, the bytes, and their count.
	 */
	void (*write)(void *ctx, const uint8_t *data, size_t len);
	void *ctx; /**< Passed back to write(). */
};

/**
 * @brief Encoder state.
 *
 * Holds the two frames of history the predictors need. Zero-initialised is
 * valid but not usable — call bbBegin() first.
 */
struct BlackboxEncoder {
	BlackboxSink sink;

	int32_t  prev[BB_FIELD_COUNT];  /**< Previous frame's values. */
	int32_t  prev2[BB_FIELD_COUNT]; /**< The one before that. */
	bool     havePrev;              /**< prev[] is populated. */
	bool     havePrev2;             /**< prev2[] is populated. */

	uint32_t iteration;      /**< Frames emitted since bbBegin(). */
	uint32_t sinceIFrame;    /**< P frames since the last I frame. */
	uint32_t iInterval;      /**< I frame every this many frames. */

	uint32_t bytesWritten;   /**< Running total, for the UI. */
	uint32_t iFrames;        /**< I frames emitted. */
	uint32_t pFrames;        /**< P frames emitted. */
};

/**
 * @defgroup bb_prim Encoding primitives
 * @brief Exposed for testing; callers normally use the frame functions.
 * @{
 */

/**
 * @brief Variable-byte encode an unsigned value.
 *
 * Seven bits per byte, high bit set on every byte but the last. Values below
 * 128 cost one byte, which is why the predictors matter so much: a field that
 * does not change costs a single zero byte per frame.
 *
 * @param v   Value.
 * @param[out] out At least 5 bytes.
 * @return    Bytes written, 1..5.
 */
uint8_t bbWriteUnsignedVB(uint32_t v, uint8_t *out);

/**
 * @brief Zig-zag a signed value into unsigned.
 *
 * Maps 0,-1,1,-2,2 to 0,1,2,3,4 so small magnitudes stay small after VB
 * encoding. Without it, -1 would encode as 0xFFFFFFFF and cost five bytes.
 *
 * @param v Value.
 * @return  Zig-zagged.
 */
uint32_t bbZigZag(int32_t v);

/**
 * @brief Variable-byte encode a signed value, zig-zagged first.
 * @param v   Value.
 * @param[out] out At least 5 bytes.
 * @return    Bytes written, 1..5.
 */
uint8_t bbWriteSignedVB(int32_t v, uint8_t *out);

/** @} */

/**
 * @brief Field names, in index order. @see bb_fields
 *
 * `loopIteration`, `time`, `vbatLatest` and `amperageLatest` are names the
 * decoder looks up specifically. The rest decode as ordinary columns.
 */
extern const char *const bbFieldNames[BB_FIELD_COUNT];

/**
 * @brief Start a log: reset state and emit the header.
 *
 * @param e         Encoder.
 * @param sink      Where bytes go. Copied.
 * @param iInterval Frames between I frames. Clamped to at least 1.
 */
void bbBegin(BlackboxEncoder *e, const BlackboxSink *sink, uint32_t iInterval);

/**
 * @brief Emit one frame, choosing I or P automatically.
 *
 * The first frame after bbBegin() is always an I frame, as is every
 * `iInterval`th frame after it. A decoder that joins mid-stream needs those to
 * resynchronise, which is the whole reason not to emit P frames forever.
 *
 * @param e       Encoder.
 * @param timeUs  Timestamp, microseconds.
 * @param values  @ref BB_FIELD_COUNT values. Indices 0 and 1 are overwritten
 *                with the iteration counter and @p timeUs.
 *
 * @warning Every field except @ref BB_F_TEMP is declared unsigned in the log
 *          header. Passing a negative value for one of those does not fail —
 *          it wraps to roughly 4.29e9 and decodes as garbage, in a file that
 *          otherwise parses without complaint. Clamp at the call site. Only
 *          temperature is declared signed, because only temperature can
 *          legitimately go below zero on a bench.
 */
void bbWriteFrame(BlackboxEncoder *e, uint32_t timeUs, const int32_t *values);

/**
 * @brief Emit an event frame.
 * @param e  Encoder.
 * @param id Event ID, e.g. @ref BB_EVENT_LOG_END.
 */
void bbWriteEvent(BlackboxEncoder *e, uint8_t id);

/**
 * @brief Close the log: emit the end-of-log event.
 *
 * A log without this is still parseable up to the last complete frame, which
 * matters because yanking power mid-write is the normal way a bench session
 * ends.
 *
 * @param e Encoder.
 */
void bbEnd(BlackboxEncoder *e);
