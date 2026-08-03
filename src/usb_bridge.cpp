/**
 * @file usb_bridge.cpp
 * @brief USB half of the bridge. Runs on core0; the wire is core1's.
 * @see bridge_wire.h for the split and why it exists.
 */

#include "usb_bridge.h"
#include "bridge_wire.h"
#include "bridge_log.h"
#include "config.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

/** @brief Largest frame forwarded in one go. */
#define BRIDGE_BUF WIRE_FRAME_MAX

/**
 * @brief Idle gap that marks the end of a host frame, in milliseconds.
 *
 * USB delivers a frame as one burst, so a short silence means the host has
 * finished. Handing over a partial frame would split it on the wire with a gap
 * in the middle, which the bootloader need not tolerate.
 */
#define BRIDGE_GATHER_MS 2

static bool           s_active = false;
static UsbBridgeStats s_stats;
static uint8_t        s_buf[BRIDGE_BUF];   /**< Host frame, and its echo. */
static uint8_t        s_rx[BRIDGE_BUF];    /**< ESC reply. */

void usbBridgeBegin(uint8_t pin) {
	memset(&s_stats, 0, sizeof(s_stats));
	bridgeLogReset();
	bridgeLogNote("bridge opened");
	bridgeWireBegin(pin);
	s_active = true;

	// Anything already queued belongs to whatever was happening before the
	// bridge opened, and would be forwarded to the ESC as noise.
	while (Serial.available()) Serial.read();
}

void usbBridgeEnd() {
	bridgeWireEnd();
	bridgeLogNote("bridge closed");

	// Dump *before* clearing the active flag. That flag is what suppresses the
	// telemetry printf in loop(), so clearing it first re-enables telemetry for
	// the whole duration of the dump -- and any yield inside the dump would
	// then interleave log lines into it.
	bridgeLogDump();

	s_active = false;
}

bool usbBridgeActive() { return s_active; }

void usbBridgeStats(UsbBridgeStats *out) { *out = s_stats; }

void usbBridgePoll() {
	if (!s_active) return;

	// --- host to ESC ---
	if (Serial.available()) {
		uint16_t n = 0;
		while (n < BRIDGE_BUF && Serial.available()) {
			s_buf[n++] = (uint8_t)Serial.read();
		}

		// Wait briefly for the rest of the frame. USB delivers a frame as one
		// burst, but the poll can land mid-burst, and a frame split here would
		// go out on the wire with a gap in the middle.
		//
		// Counting yielded milliseconds rather than spinning on millis() keeps
		// this bounded by construction: a spin loop that waits for a clock it
		// never lets advance is an infinite loop, which is exactly what the
		// first version of this did under the tests' virtual clock.
		for (int idleMs = 0; idleMs < BRIDGE_GATHER_MS && n < BRIDGE_BUF; ) {
			if (Serial.available()) {
				while (n < BRIDGE_BUF && Serial.available()) {
					s_buf[n++] = (uint8_t)Serial.read();
				}
				idleMs = 0;
			} else {
				delay(1);
				idleMs++;
			}
		}

		if (n) {
			// Hand the frame to core1. It only refuses while a previous frame
			// is still going out, so waiting is the correct response --
			// dropping it would corrupt the exchange, and the host is not
			// talking again until it has an answer anyway.
			while (!bridgeWireSend(s_buf, n)) delay(1);

			// No echo here. Core1 feeds each byte back as it is clocked out, so
			// the echo reaches the host through the same path as the reply, in
			// order and at wire speed.
			//
			// Echoing the whole frame here instead -- which is what this used to
			// do -- delivers it as one instant burst. The reference tool then
			// reads a large chunk that does not end in 0x30, discards it, and
			// the ACK arrives alone a hundred milliseconds later, where it can
			// never satisfy that tool's `len > 1` test. @see am32WriteRawEchoed()
			bridgeLogAdd(BLOG_HOST_TO_ESC, s_buf, n);
			s_stats.toEsc += n;
			s_stats.frames++;
			s_stats.lastMs = millis();
		}
	}

	// --- ESC to host ---
	// Just a drain of what core1 has already collected, so this never blocks and
	// nothing is lost if core0 was away repainting.
	uint16_t r = bridgeWireRecv(s_rx, BRIDGE_BUF);
	if (r) {
		bridgeLogAdd(BLOG_ESC_TO_HOST, s_rx, r);
		Serial.write(s_rx, r);
		Serial.flush();
		s_stats.toHost += r;
		s_stats.lastMs = millis();
	}
}
