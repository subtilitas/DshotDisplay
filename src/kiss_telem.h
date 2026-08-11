/**
 * @file kiss_telem.h
 * @brief KISS ESC one-wire telemetry decoder.
 *
 * A ten-byte frame at 115200 8N1 on a dedicated wire from the ESC's telemetry
 * pad, sent in response to a DShot frame with the telemetry-request bit set.
 * Carries voltage and current at far finer resolution than Extended DShot
 * Telemetry manages inside the eRPM frame — 0.01 V against 0.25 V, 0.01 A
 * against 1 A — plus a consumption figure EDT has no room for at all.
 *
 * Frame layout, confirmed against Betaflight's `src/main/sensors/esc_sensor.c`:
 *
 * | Byte | Field       | Encoding                        |
 * |------|-------------|---------------------------------|
 * | 0    | Temperature | °C, unsigned                    |
 * | 1..2 | Voltage     | big-endian u16, 0.01 V/LSB      |
 * | 3..4 | Current     | big-endian u16, 0.01 A/LSB      |
 * | 5..6 | Consumption | big-endian u16, 1 mAh/LSB       |
 * | 7..8 | eRPM        | big-endian u16, 100 eRPM/LSB    |
 * | 9    | CRC8        | over bytes 0..8                 |
 *
 * @note Big-endian, unlike the rest of the DShot family. Easy to get backwards.
 *
 * This header is deliberately free of Arduino and Pico SDK dependencies so the
 * decoder can be exercised on the host. Reading bytes off a UART is the
 * caller's problem; feed them here with kissFeed().
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** @brief Wire size of one telemetry frame, CRC included. */
#define KISS_FRAME_LEN 10

/** @brief Baud rate the KISS protocol mandates. Not configurable at the ESC. */
#define KISS_BAUD      115200

/**
 * @brief One decoded frame, in physical units.
 *
 * Every field is filled from the same frame, so unlike @ref EscTelemetry there
 * are no per-field validity flags: a frame either passes CRC whole or is
 * discarded whole.
 */
struct KissFrame {
	int16_t  tempC;   /**< ESC temperature, degrees Celsius. */
	float    volts;   /**< Pack voltage. Resolution 0.01 V. */
	float    amps;    /**< Current draw. Resolution 0.01 A. */
	uint16_t mah;     /**< Consumption since the ESC powered on, not since arm. */
	uint32_t erpm;    /**< Electrical RPM. Resolution 100 eRPM. */
};

/**
 * @brief Byte-at-a-time frame assembler.
 *
 * KISS has no start delimiter, so there is nothing in the byte stream that says
 * "a frame begins here". Synchronisation comes from the request boundary
 * instead: the caller calls kissExpectFrame() when it asks the ESC for
 * telemetry, and the ten bytes that follow are the reply.
 *
 * Zero-initialised is a valid starting state.
 */
struct KissDecoder {
	uint8_t  buf[KISS_FRAME_LEN]; /**< Partial frame. */
	uint8_t  have;                /**< Bytes buffered so far, 0..KISS_FRAME_LEN. */
	uint32_t good;                /**< Frames that passed CRC. */
	uint32_t bad;                 /**< Frames that failed CRC. */
	uint32_t overrun;             /**< Bytes dropped arriving after a full frame. */
};

/**
 * @brief CRC8 over a buffer.
 *
 * Polynomial 0x07, init 0x00, no reflection, no final XOR — plain CRC-8, and
 * specifically **not** the 0xD5 variant that several third-hand descriptions of
 * this protocol claim. Matches Betaflight's `calculateCrc8()` bit for bit.
 *
 * @param buf Bytes to sum.
 * @param len Number of bytes.
 * @return    The checksum.
 */
uint8_t kissCrc8(const uint8_t *buf, uint8_t len);

/**
 * @brief Reset the assembler and arm it for an incoming reply.
 *
 * Call when the telemetry-request bit goes out. Discards any partial frame,
 * which is the correct thing to do: a frame that was still incomplete when the
 * next request went out is never going to finish.
 *
 * @param d Decoder state.
 */
void kissExpectFrame(KissDecoder *d);

/**
 * @brief Feed one received byte.
 *
 * @param d   Decoder state.
 * @param b   The byte.
 * @param[out] out Filled only when the return value is true.
 * @return    True if this byte completed a frame that passed CRC.
 *
 * @note A frame that completes but fails CRC returns false and increments
 *       @ref KissDecoder::bad. Either way the buffer is emptied, so the next
 *       byte starts a fresh frame.
 */
bool kissFeed(KissDecoder *d, uint8_t b, KissFrame *out);

/**
 * @brief Decode ten bytes that are already known to be frame-aligned.
 *
 * Split out from kissFeed() so tests and callers holding a complete buffer do
 * not have to pretend to be a byte stream.
 *
 * @param buf     Exactly @ref KISS_FRAME_LEN bytes.
 * @param[out] out Decoded frame. Untouched if the CRC fails.
 * @return        True if the CRC matched.
 */
bool kissDecodeFrame(const uint8_t *buf, KissFrame *out);

/**
 * @brief Build the 12-bit DShot payload for `BidirDShotX1::sendRaw12Bit()`.
 *
 * The DShot library cannot express "throttle, and please send telemetry":
 * `sendThrottle()` always clears the request bit and `sendRaw11Bit()` always
 * sets it. `sendRaw12Bit()` is public though, so the 12-bit value is assembled
 * here instead and no fork of the library is needed.
 *
 * @param throttle0to2000 Throttle. Clamped to 2000.
 * @param requestTelemetry Sets the telemetry-request bit.
 * @return The 12-bit value: 11 bits of throttle, then the request bit.
 *
 * @warning Zero must stay zero. `sendThrottle()` adds the 47-command offset
 *          only for non-zero throttles so that 0 means motor stop, and skipping
 *          that special case would turn "stop" into a small forward command.
 */
uint16_t kissBuildDshotPayload(uint16_t throttle0to2000, bool requestTelemetry);
