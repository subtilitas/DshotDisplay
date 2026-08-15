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
 * @brief Start the frame pump. Call from core1.
 *
 * Does not itself claim the PIO state machine: the driver is constructed on the
 * first escTaskPoll(), from whatever configuration escTaskConfigure() last
 * supplied, and that same path rebuilds it after a suspend or a pin change. One
 * construction site rather than two is the point — the version of this function
 * that also built a driver read `initError` off a pointer that was still null,
 * which on RP2350 reads low flash and reports whatever happens to be there.
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
 * @brief Queue a beacon command so the motor beeps.
 *
 * @param n Beacon 1..5, clamped.
 * @return False if the request was refused because the ESC is armed. The
 *         caller is expected to say so: a command that is silently dropped is
 *         indistinguishable from a button that does not work, which is exactly
 *         how it was reported.
 */
bool escRequestBeep(uint8_t n);

/** @brief What the automatic EDT enable should do this frame. */
enum class EdtAutoAction : uint8_t {
	None,   /**< Nothing to do: no ESC, EDT already working, or too soon. */
	Send,   /**< An ESC is answering and is not sending EDT. Ask it again. */
	Rearm,  /**< The ESC went away; the next one starts from a clean slate. */
};

/**
 * @brief Decide whether to send the automatic EDT enable. Pure; host-testable.
 *
 * This rule has been wrong twice, in the same direction both times, so it is
 * worth writing down what it is actually for.
 *
 * First it went out once, on a timer, 1.5 s after boot — fine for an ESC
 * already plugged in and powered, useless for one connected afterwards,
 * power-cycled, or swapped. Then it went out once, on the first eRPM frame,
 * which fixed the "connected afterwards" case and kept the deeper mistake: it
 * was still a one-shot, and it fired at the earliest instant an ESC could
 * possibly be heard from. An ESC answers eRPM within milliseconds of power-up
 * and will not act on a DShot command until it has seen a run of valid
 * zero-throttle frames, so the single attempt was made at close to the least
 * likely moment for it to be accepted. Nothing tried again.
 *
 * Both versions failed the same way — an ESC reporting RPM and nothing else,
 * which is exactly what an ESC without EDT support looks like — and both were
 * masked by the same accident: changing any wiring setting rebuilds the pump,
 * which cleared the flag and sent a second enable at a moment the ESC was ready
 * for. "EDT only comes on if I toggle KISS" is that accident, reported.
 *
 * The third version made the condition "is it working" rather than "have we
 * asked", and retried on an interval — and still went off sometimes, with a
 * disarm and re-arm as the reliable way to get it back. That is a tell, because
 * an arm transition is the one event that clears the `tried` flag, so what it
 * bought was an attempt *now* instead of one up to @ref EDT_RETRY_MS away. The
 * attempts themselves were the problem, not their number:
 *
 * - the first went out on the first eRPM frame, before the ESC would take it,
 *   and still counted as an attempt (@ref EDT_SETTLE_MS);
 * - a burst could be truncated by the beacon command sharing the same queue
 *   slot, and a truncated burst also counted (@ref EDT_ENABLE_REPEATS);
 * - and one could go out with the motor still coasting down after a disarm,
 *   which no ESC executes.
 *
 * Hence @p canCommand: the caller asserts the ESC is in a state where the
 * enable can actually be executed and where a burst will not be cut short. The
 * interval then governs genuine attempts rather than counting discarded ones.
 * @see EDT_RETRY_MS
 *
 * Split out as a pure function so the rule is testable — esc_task.cpp pulls in
 * the PIO library and cannot be linked into the host suite.
 *
 * @param linkUp     True if an eRPM frame arrived within @ref ESC_LINK_STALE_MS.
 * @param edtFresh   True if any EDT frame arrived within @ref EDT_STALE_MS.
 * @param canCommand True if the ESC would act on a command sent now: disarmed,
 *                   motor stopped, link settled, and no burst already going out.
 * @param tried      True if an enable has gone out in full to the ESC now connected.
 * @param sinceMs    Milliseconds since that enable. Ignored when @p tried is false.
 * @param retryMs    How long an unanswered enable is left before repeating it.
 * @return           What to do.
 */
static inline EdtAutoAction edtAutoAction(bool linkUp, bool edtFresh,
                                          bool canCommand, bool tried,
                                          uint32_t sinceMs, uint32_t retryMs) {
	// No ESC. Forget what was sent to the last one, once.
	if (!linkUp) return tried ? EdtAutoAction::Rearm : EdtAutoAction::None;
	// It is working. That is the whole success condition; nothing else to do
	// until the link drops.
	if (edtFresh) return EdtAutoAction::None;
	// Deliberately ahead of the interval check, and it leaves `tried` alone:
	// a moment the ESC cannot be commanded in is not an attempt, and must not
	// start the clock on the next one.
	if (!canCommand) return EdtAutoAction::None;
	if (tried && sinceMs < retryMs) return EdtAutoAction::None;
	return EdtAutoAction::Send;
}

/** @brief What core1 should put on the wire in one frame slot. */
struct EscFrame {
	bool     sendCommand;  /**< Send @ref command instead of a throttle value. */
	uint8_t  command;      /**< DShot command, meaningful when @ref sendCommand. */
	uint16_t throttle;     /**< Throttle to send, when not sending a command. */
	bool     requestKiss;  /**< Set the frame's telemetry-request bit. */
};

/**
 * @brief Decide what one DShot frame carries. Pure; host-testable.
 *
 * Extracted for the same reason edtAutoAction() was: esc_task.cpp pulls in the
 * PIO library and the SDK's UART and cannot be linked into the host suite, so
 * any rule left inside it is a rule no test can reach. Three of the rules here
 * are safety interlocks, and all three were untested — a mutation deleting the
 * heartbeat check, the disarm-zeroing, or the refusal to send commands while
 * armed passed the entire suite.
 *
 * The rules, in the order they matter:
 *
 * - **A dead UI means zero throttle.** If core0 has stopped calling
 *   escHeartbeat(), core1 stops believing the throttle it was last given. This
 *   is the backstop against a hung display leaving a motor running, and it is
 *   the only one that does not depend on core0 being well enough to act.
 * - **Disarmed means zero throttle**, regardless of what was last commanded.
 * - **Commands only go out while disarmed.** An ESC ignores them while armed
 *   anyway, so sending one is at best noise in the frame stream; and a queued
 *   command must not be consumed by a frame that cannot deliver it, or it is
 *   silently lost.
 *
 * @param armed       Arm state.
 * @param uiAlive     Core0 has checked in within @ref UI_HEARTBEAT_TIMEOUT_MS.
 * @param throttle    Throttle core0 last commanded, 0..2000.
 * @param pendingCmd  Queued DShot command.
 * @param pendingReps Repeats still owed for @p pendingCmd; 0 means none queued.
 * @param kissWanted  This slot is due to request KISS telemetry.
 * @return What to send.
 */
static inline EscFrame escFrameAction(bool armed, bool uiAlive, uint16_t throttle,
                                      uint8_t pendingCmd, uint8_t pendingReps,
                                      bool kissWanted) {
	EscFrame f = {false, 0, 0, false};
	if (pendingReps > 0 && !armed) {
		f.sendCommand = true;
		f.command = pendingCmd;
		return f;
	}
	f.throttle = (!armed || !uiAlive) ? 0 : throttle;
	// Never alongside a command: sendRaw11Bit() forces the request bit set
	// anyway, and a reply arriving mid-sequence is noise the decoder cannot
	// distinguish from a real one.
	f.requestKiss = kissWanted && pendingReps == 0;
	return f;
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

/**
 * @brief Set the pins and bitrate the DShot pump uses, and rebuild it.
 *
 * Core0 hands the values across rather than core1 reading the settings
 * struct directly: core0 owns that struct and edits it a field at a time while the
 * setup screen is open, so core1 reading it would see half-applied
 * combinations — a new pin against an old bitrate, for one frame.
 *
 * Takes effect on the next escTaskPoll(): the driver is destroyed and rebuilt,
 * which releases the old pin and claims the new one. Forces a disarm, because
 * the ESC on the old pin stops hearing frames the moment it is released.
 *
 * Safe to call with unchanged values; it is a no-op if nothing differs, so the
 * setup screen can call it freely rather than tracking what it changed.
 *
 * @param dshotPin   GPIO for the ESC signal wire.
 * @param dshotKbaud DShot bitrate.
 * @param kissEnable Claim a UART for KISS telemetry.
 * @param kissPin    GPIO for the telemetry wire. Ignored unless @p kissEnable.
 */
void escTaskConfigure(uint8_t dshotPin, uint16_t dshotKbaud,
                      bool kissEnable, uint8_t kissPin);

/** @brief GPIO the pump is currently driving. @return The live ESC pin. */
uint8_t escTaskDshotPin();

/** @} */
