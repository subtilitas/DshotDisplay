/**
 * @file bridge_log.h
 * @brief Timestamped capture of USB-bridge traffic, dumped on exit.
 *
 * Records every transfer in both directions into a RAM ring buffer while the
 * bridge runs, then prints an annotated hex dump over the same USB port once
 * bridging stops. Printing during a session is not an option — the port is
 * carrying the forwarded bytes.
 *
 * Timing is recorded because the interesting bridge failures are about *when*
 * bytes arrive, not what they are. A host polling `read_all()` every 25 ms sees
 * one flush as one batch, so a reply arriving 60 ms after its echo is a
 * different bug from a reply that is simply wrong, and only the timestamps
 * tell them apart.
 *
 * Record format in the ring, little-endian:
 *
 *     [ms:4][dir:1][len:1][data:len]
 */

#pragma once

#include <stdint.h>

/** @brief Direction of a captured transfer. */
enum BridgeLogDir : uint8_t {
	BLOG_HOST_TO_ESC = 0, /**< Forwarded from USB onto the one-wire line. */
	BLOG_ESC_TO_HOST = 1, /**< Received from the ESC and sent to USB. */
	BLOG_NOTE        = 2, /**< A marker rather than wire traffic. */
};

/** @brief Discard the capture and restart timing from now. */
void bridgeLogReset();

/**
 * @brief Record one transfer.
 *
 * Oldest records are dropped once the buffer fills, so a long session keeps
 * the most recent traffic rather than the first few frames.
 *
 * @param dir  Which way the bytes went.
 * @param data Bytes transferred.
 * @param len  Number of bytes; longer transfers are truncated.
 */
void bridgeLogAdd(BridgeLogDir dir, const uint8_t *data, uint16_t len);

/** @brief Record a short text marker in the timeline. */
void bridgeLogNote(const char *text);

/**
 * @brief Print the capture over USB serial as an annotated hex dump.
 *
 * Only meaningful once bridging has stopped.
 */
void bridgeLogDump();

/** @brief Records currently held. */
uint16_t bridgeLogCount();

/** @brief True if records have been dropped to make room. */
bool bridgeLogOverflowed();
