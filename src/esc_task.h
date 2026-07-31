/**
 * @file esc_task.h
 * @brief Bidirectional DShot driver task. Runs on core1.
 *
 * Core1 owns the `BidirDShotX1` instance and does nothing but push frames at a
 * steady @ref DSHOT_PERIOD_US and drain telemetry. That keeps DShot timing
 * immune to the multi-millisecond SPI DMA bursts the display does on core0.
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
 * Fields whose `have*` flag is false were never sent by this ESC — plain eRPM
 * always works on bidirectional DShot, but the rest requires Extended DShot
 * Telemetry support in the ESC firmware.
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

	uint32_t lastRpmMs;     /**< millis() of the last valid eRPM frame. */
	bool     haveVolts;     /**< True once a voltage frame has arrived. */
	bool     haveAmps;      /**< True once a current frame has arrived. */
	bool     haveTemp;      /**< True once a temperature frame has arrived. */
	bool     haveStress;    /**< True once a stress frame has arrived. */
	bool     initError;     /**< PIO state machine setup failed. */
};

/**
 * @defgroup esc_core1 Core1 entry points
 * @{
 */

/** @brief Claim the PIO state machine and start the frame pump. Call from setup1(). */
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
 * only while the ESC is disarmed. No-op if currently armed. The firmware also
 * issues this automatically 1.5 s after boot.
 */
void escRequestEdtEnable();

/**
 * @brief Queue a beacon command so the motor beeps.
 * @param n Beacon 1..5, clamped. No-op if currently armed.
 */
void escRequestBeep(uint8_t n);

/**
 * @brief Copy the current telemetry block.
 * @param[out] out Destination. Filled under the critical section.
 */
void escSnapshot(EscTelemetry *out);

/** @brief True once an EDT enable has been issued. @return Request state. */
bool escEdtRequested();

/** @} */
