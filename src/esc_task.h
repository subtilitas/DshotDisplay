/**
 * @file esc_task.h
 * @brief Bidirectional DShot driver task. Runs on core1.
 *
 * Core1 owns the `BidirDShotX1` instance and does nothing but push frames at a
 * steady @ref DSHOT_PERIOD_US and drain telemetry. That keeps DShot timing
 * immune to the multi-millisecond SPI DMA bursts the display does on core0.
 *
 * The ESC's answer to a frame lands 30 us after that frame ends (fixed by the
 * bidirectional-DShot spec, independent of speed), so each slot first drains
 * the previous frame's reply, then sends the next one.
 *
 * Core0 talks to it through a small critical-section-guarded state block:
 * escSetThrottle() and escSetArmed() to command, escSnapshot() to read back.
 *
 * @warning Core0 must call escHeartbeat() regularly. If it stops for longer
 *          than @ref UI_HEARTBEAT_TIMEOUT_MS, core1 assumes the UI has died and
 *          forces the throttle to zero on its own.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Everything core1 has decoded from the ESC.
 *
 * A field whose arrival time is 0 was never sent by this ESC — plain eRPM always
 * works on bidirectional DShot, but the rest requires Extended DShot Telemetry
 * support in the ESC firmware. A field whose arrival time has simply fallen
 * behind is a different thing: that ESC does send it, and has stopped. Use
 * escFieldFresh() rather than reading the values bare.
 */
struct EscTelemetry {
	uint32_t erpm;          /**< Raw electrical RPM from the ESC. */
	uint32_t rpm;           /**< Mechanical RPM: `erpm / (poles / 2)`. */
	float    volts;         /**< EDT voltage frame, converted from 250 mV steps. */
	float    amps;          /**< EDT current frame, 1 A steps. */
	int16_t  tempC;         /**< EDT temperature frame, degrees Celsius. */
	uint8_t  stress;        /**< EDT stress frame, 0..255. */
	uint8_t  statusRaw;     /**< EDT status frame, undecoded. */
	uint8_t  maxStress;     /**< Status bits 3..0: max stress level, 0..15. */
	bool     alert;         /**< Status bit 7. Meaning is ESC-firmware specific. */
	bool     warning;       /**< Status bit 6. Meaning is ESC-firmware specific. */
	bool     error;         /**< Status bit 5. Meaning is ESC-firmware specific. */

	uint32_t goodPackets;   /**< Frames decoded successfully since boot. */
	uint32_t badPackets;    /**< Checksum failures since boot. */
	uint32_t noPackets;     /**< Frames the ESC did not answer at all. */
	uint16_t packetRate;    /**< Good packets per second, updated once a second. */
	uint8_t  errPercent;    /**< Checksum error rate over the last second. */

	/**
	 * @name Arrival times
	 *
	 * When each kind of frame last arrived, as millis(); 0 means never. These
	 * are timestamps rather than `have` flags on purpose. A sticky boolean can
	 * only ever go true, so unplugging the ESC — or swapping it for another —
	 * left the last voltage, current and temperature on screen indefinitely,
	 * looking exactly like live data from hardware that was no longer there.
	 *
	 * EDT frame types are interleaved by the ESC and each cycles far quicker
	 * than @ref EDT_STALE_MS, so per-field expiry costs nothing in practice and
	 * localises the loss when only one frame type stops.
	 *
	 * @{
	 */
	uint32_t lastRpmMs;     /**< Last valid eRPM frame. */
	uint32_t edtVoltsMs;    /**< Last EDT voltage frame. */
	uint32_t edtAmpsMs;     /**< Last EDT current frame. */
	uint32_t edtTempMs;     /**< Last EDT temperature frame. */
	uint32_t edtStressMs;   /**< Last EDT stress frame. */
	uint32_t edtStatusMs;   /**< Last EDT status frame. */
	/** @} */

	bool     initError;     /**< PIO state machine setup failed. */

	/**
	 * @name KISS telemetry
	 * From the separate telemetry wire, not the DShot signal line. Kept
	 * distinct from the EDT fields above rather than overwriting them, so the
	 * two can be compared against each other during bring-up — which is how
	 * the KISS decoder gets validated against a source already trusted.
	 * @see escMerge(), docs/design/kiss-telemetry.md
	 * @{
	 */
	float    kissVolts;     /**< Pack voltage, 0.01 V resolution. */
	float    kissAmps;      /**< Current, 0.01 A resolution. */
	int16_t  kissTempC;     /**< ESC temperature, degrees Celsius. */
	uint16_t kissMah;       /**< Consumption since ESC power-on, not since arm. */
	uint32_t kissErpm;      /**< Electrical RPM, 100 eRPM steps. */
	uint32_t kissLastMs;    /**< millis() of the last CRC-valid frame. */
	uint32_t kissGood;      /**< Frames that passed CRC since boot. */
	uint32_t kissBad;       /**< Frames that failed CRC since boot. */
	uint32_t kissTimeouts;  /**< Requests that drew no complete reply. */
	bool     haveKiss;      /**< True once any frame has been decoded. */
	/** @} */
};

/**
 * @defgroup esc_core1 Core1 entry points
 * @{
 */

/**
 * @brief Initialise the cross-core state. Call from **core0**, before launching core1.
 *
 * Separate from escTaskBegin() because of an ordering hazard that is invisible
 * until it bites: escSnapshot() takes a critical section, core0 calls it on its
 * very first UI frame, and a critical section must be initialised before anyone
 * enters it. If that initialisation lived on core1, core0 would reach
 * escSnapshot() first — core1 takes a moment to come up — and block forever on
 * an uninitialised spin lock.
 *
 * The symptom is a board that shows the splash and then nothing at all.
 */
void escTaskInit();

/**
 * @brief Claim the PIO state machine and start the frame pump. Call from core1.
 *
 * @pre escTaskInit() has been called on core0.
 */
void escTaskBegin();

/** @brief Service one frame slot. Call from loop1() as fast as possible. */
void escTaskPoll();

/** @} */

/**
 * @defgroup esc_core0 Core0 API
 * @brief Safe to call from core0; each takes the shared critical section.
 * @{
 */

/**
 * @brief Set the commanded throttle.
 * @param throttle0to2000 Throttle, clamped to 0..2000. 0 is motor stop.
 *                        Ignored by core1 while disarmed.
 */
void escSetThrottle(uint16_t throttle0to2000);

/**
 * @brief Arm or disarm.
 * @param armed false immediately zeroes the throttle as well.
 */
void escSetArmed(bool armed);

/**
 * @brief Set the motor pole count used for the eRPM to RPM conversion.
 * @param poles Magnet count; values below 2 are clamped.
 */
void escSetPoles(uint8_t poles);

/**
 * @brief Tell core1 that core0 is still alive.
 * @see UI_HEARTBEAT_TIMEOUT_MS
 */
void escHeartbeat();

/**
 * @brief Queue `DSHOT_CMD_EXTENDED_TELEMETRY_ENABLE`, repeated 10 times.
 *
 * The DShot spec requires six consecutive receptions for a command to take, and
 * only while the ESC is disarmed. The firmware also issues this by itself once
 * per ESC, as soon as one starts answering; see edtAutoAction().
 *
 * @return False if the request was refused because the ESC is armed. The caller
 *         is expected to say so: a command that is silently dropped is
 *         indistinguishable from a button that does not work, which is exactly
 *         how it was reported.
 */
bool escRequestEdtEnable();

/**
 * @brief Queue a beacon command so the motor beeps.
 * @param n Beacon 1..5, clamped.
 * @return False if refused because the ESC is armed. @see escRequestEdtEnable
 */
bool escRequestBeep(uint8_t n);

/** @brief What the automatic EDT enable should do this frame. */
enum class EdtAutoAction : uint8_t {
	None,   /**< Nothing to do. */
	Send,   /**< An ESC is answering and has not been sent an enable yet. */
	Rearm,  /**< The ESC went away; the next one gets its own enable. */
};

/**
 * @brief Decide whether to send the automatic EDT enable. Pure; host-testable.
 *
 * The enable used to go out once, on a timer, 1.5 s after boot. That is fine
 * for an ESC that is already plugged in and powered, and useless for every
 * other case: connect the ESC afterwards, power-cycle it, or swap it for a
 * different one, and it never receives the enable at all. eRPM keeps working —
 * that is plain bidirectional DShot — so the symptom is an ESC that reports
 * RPM and nothing else, which looks exactly like an ESC without EDT support.
 *
 * Waiting for eRPM instead is both later and more reliable: an ESC that has
 * answered a frame is demonstrably powered, booted and listening, which a
 * 1.5 s timer only assumed.
 *
 * Split out as a pure function so the rule is testable — esc_task.cpp pulls in
 * the PIO library and the SDK's UART, and cannot be linked into the host suite.
 *
 * @param linkUp True if an eRPM frame arrived within @ref ESC_LINK_STALE_MS.
 * @param sent   True if this ESC has already been sent an enable.
 * @return       What to do.
 */
static inline EdtAutoAction edtAutoAction(bool linkUp, bool sent) {
	if (!linkUp) return sent ? EdtAutoAction::Rearm : EdtAutoAction::None;
	return sent ? EdtAutoAction::None : EdtAutoAction::Send;
}

/**
 * @brief Copy the current telemetry block.
 * @param[out] out Destination. Filled under the critical section.
 */
void escSnapshot(EscTelemetry *out);

/** @brief True once an EDT enable has been issued. @return Request state. */
bool escEdtRequested();

/**
 * @brief Ask core1 to tear down the DShot driver and release the signal pin.
 *
 * Needed before the AM32 bootloader transport can use the same pin. Forces a
 * disarm first. Poll escTaskSuspended() until it reports true; the teardown
 * happens on core1 so that the PIO state machine is freed by the core that
 * claimed it.
 */
void escTaskSuspend();

/** @brief Ask core1 to rebuild the DShot driver. Starts disarmed. */
void escTaskResume();

/** @brief True once the driver is torn down and the pin is free. */
bool escTaskSuspended();

/** @} */
