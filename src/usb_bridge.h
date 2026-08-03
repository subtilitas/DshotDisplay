/**
 * @file usb_bridge.h
 * @brief Transparent USB-serial to one-wire bridge.
 *
 * Turns the board into a USB-to-one-wire adapter so a desktop AM32
 * configurator can talk to the ESC through it — including flashing firmware,
 * which the device deliberately does not implement itself. Reusing a tool that
 * already works on real hardware beats reimplementing image transfer on a
 * 240x320 touchscreen.
 *
 * @section bridge_echo Why transmitted bytes are echoed back
 *
 * On a genuine one-wire link the host's receiver sees its own transmission,
 * because both ends share the conductor. Desktop tools depend on that. The
 * reference configurator's ACK check is:
 *
 * ```
 * if len(self.last_result) > 1:
 *     if int(self.last_result[-1]) == 0x30:
 * ```
 *
 * A bare `SET_ADDRESS` is answered with a single 0x30 byte, so without the echo
 * the reply is one byte long, `len > 1` is false, and the host reports NACK on
 * the very first command of every session.
 *
 * That the tool indexes replies from the end — `[-1]`, `[-5]`,
 * `[-(size+3):-3]` — is what makes the leading echo harmless.
 *
 * @section bridge_transparent Why the bridge is transparent, not transactional
 *
 * The echo goes out as soon as the bytes are on the wire; the bridge never
 * waits to see whether the ESC answers. It is tempting to hold the echo back
 * and deliver it together with the reply, so that both land in one of the
 * host's reads — but that breaks writing.
 *
 * `SET_BUFFER` is the one command the bootloader does not acknowledge. A bridge
 * that waits for a reply stalls its whole timeout there, while the host has
 * already moved on: it sleeps 25 ms, calls `flushInput()` to discard the echo,
 * and sends the payload. The delayed echo then arrives *after* that flush and
 * corrupts the payload exchange. Settings reads still work, so the damage looks
 * like "flashing is broken" rather than "the bridge is wrong".
 *
 * A plain USB-serial adapter has no concept of a transaction, and the host's
 * own 25 ms sleep is what groups an echo with its reply. The bridge must not
 * try to do that job for it.
 *
 * @warning Drives the same pin as DShot. escTaskSuspend() must have completed
 *          before usbBridgeBegin().
 */

#pragma once

#include <stdint.h>

/** @brief Traffic counters, for the on-screen activity display. */
struct UsbBridgeStats {
	uint32_t toEsc;    /**< Bytes forwarded host to ESC. */
	uint32_t toHost;   /**< Bytes forwarded ESC to host. */
	uint32_t frames;   /**< Host frames forwarded. */
	uint32_t lastMs;   /**< millis() of the last traffic in either direction. */
};

/**
 * @brief Claim the signal pin and start bridging.
 * @param pin GPIO wired to the ESC signal line.
 */
void usbBridgeBegin(uint8_t pin);

/** @brief Stop bridging and release the pin. */
void usbBridgeEnd();

/** @brief True while the bridge owns the USB serial port. */
bool usbBridgeActive();

/**
 * @brief Pump traffic in both directions. Call as often as possible.
 *
 * Returns quickly when idle, so the caller can still service its UI.
 */
void usbBridgePoll();

/** @brief Read the traffic counters. @param[out] out Destination. */
void usbBridgeStats(UsbBridgeStats *out);
