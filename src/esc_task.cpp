#include "esc_task.h"
#include "config.h"
#include "kiss_telem.h"

#include "plat.h"
#include <string.h>
#include <PIO_DShot.h>
#include <pico/critical_section.h>
#include <hardware/uart.h>
#include <hardware/gpio.h>

/**
 * @file esc_task.cpp
 * @brief Core1 DShot frame pump, telemetry decode and cross-core state.
 */

static BidirDShotX1 *s_esc = nullptr; /**< Owned by core1. */

/** @brief Guards @ref s_tel against concurrent access from core0. */
static critical_section_t s_cs;

/**
 * @defgroup esc_cmd Command state (core0 writes, core1 reads)
 * @{
 */
static volatile uint16_t s_throttle    = 0;   /**< Commanded throttle, 0..2000. */
static volatile bool     s_armed       = false; /**< Arm state. */
static volatile uint8_t  s_poles       = DEFAULT_MOTOR_POLES; /**< Pole count. */
static volatile uint32_t s_heartbeatMs = 0;   /**< millis() of the last heartbeat. */
static volatile uint8_t  s_pendingCmd  = 0;   /**< Queued DShot command. */
static volatile uint8_t  s_pendingReps = 0;   /**< Repeats left for that command. */
static volatile bool     s_edtRequested = false; /**< EDT enable has been issued. */
/** @} */

/** @brief Telemetry block. Core1 writes, core0 reads via escSnapshot(). */
static EscTelemetry s_tel;

/**
 * @defgroup esc_priv Core1 private state
 * @{
 */
static uint32_t s_nextSendUs = 0;   /**< Deadline for the next frame. */
static uint32_t s_lastRateMs = 0;   /**< Last time the rate counters rolled up. */
static uint32_t s_rateGoodMark = 0; /**< goodPackets at the last roll-up. */
static uint32_t s_rateBadMark = 0;  /**< badPackets at the last roll-up. */
static bool     s_edtAutoDone = false; /**< Automatic EDT enable has fired. */
/** @} */

#if KISS_TELEM_ENABLE
/**
 * @defgroup esc_kiss KISS telemetry state (core1 only)
 * @{
 */
static KissDecoder s_kiss;              /**< Frame assembler. */
static uint16_t    s_kissCountdown = 0; /**< Frames left before the next request. */
static bool        s_kissPending = false; /**< A reply is outstanding. */
static uint32_t    s_kissReqMs = 0;     /**< millis() of the outstanding request. */

/**
 * @brief Drain whatever the UART has and fold any complete frame into @ref s_tel.
 *
 * Called from escTaskPoll() on core1. Bounded work: the RP2350 UART FIFO holds
 * 32 bytes, comfortably more than one 10-byte frame, so this reads at most a
 * FIFO's worth and never blocks. That is the whole reason it is safe to do here
 * rather than handing the UART to core0 — core1's only job is DShot timing.
 */
static void kissDrain(uint32_t ms) {
	KissFrame f;

	while (uart_is_readable(KISS_UART)) {
		uint8_t c = uart_getc(KISS_UART);
		if (kissFeed(&s_kiss, c, &f)) {
			s_kissPending = false;
			critical_section_enter_blocking(&s_cs);
			s_tel.kissVolts  = f.volts;
			s_tel.kissAmps   = f.amps;
			s_tel.kissTempC  = f.tempC;
			s_tel.kissMah    = f.mah;
			s_tel.kissErpm   = f.erpm;
			s_tel.kissLastMs = ms;
			s_tel.kissGood   = s_kiss.good;
			s_tel.haveKiss   = true;
			critical_section_exit(&s_cs);
		}
	}

	// A reply that never completed. Count it and drop the partial frame, or the
	// next request's bytes would append to it and decode at the wrong offset.
	if (s_kissPending && (uint32_t)(ms - s_kissReqMs) > KISS_REPLY_TIMEOUT_MS) {
		s_kissPending = false;
		kissExpectFrame(&s_kiss);
		critical_section_enter_blocking(&s_cs);
		s_tel.kissTimeouts++;
		s_tel.kissBad = s_kiss.bad;
		critical_section_exit(&s_cs);
	} else if (s_kiss.bad) {
		critical_section_enter_blocking(&s_cs);
		s_tel.kissBad = s_kiss.bad;
		critical_section_exit(&s_cs);
	}
}
/** @} */
#endif
void escSetThrottle(uint16_t t) {
	if (t > 2000) t = 2000;
	s_throttle = t;
}

void escSetArmed(bool armed) {
	if (!armed) s_throttle = 0;
	s_armed = armed;
}

void escSetPoles(uint8_t poles) {
	if (poles < 2) poles = 2;
	s_poles = poles;
}

void escHeartbeat() {
	s_heartbeatMs = millis();
}

void escRequestEdtEnable() {
	if (s_armed) return;               // commands are only valid when disarmed
	s_pendingCmd  = DSHOT_CMD_EXTENDED_TELEMETRY_ENABLE;
	s_pendingReps = 10;                // spec asks for 6; a few spare frames
	s_edtRequested = true;
}

void escRequestBeep(uint8_t n) {
	if (s_armed) return;
	if (n < 1) n = 1;
	if (n > 5) n = 5;
	s_pendingCmd  = (uint8_t)(DSHOT_CMD_BEACON1 + (n - 1));
	s_pendingReps = 6;
}

bool escEdtRequested() { return s_edtRequested; }

/**
 * @defgroup esc_suspend Pin handover
 * @brief Lets the AM32 bootloader transport borrow the signal pin.
 * @{
 */
static volatile bool s_suspendReq = false;
static volatile bool s_suspended  = false;

void escTaskSuspend() {
	escSetArmed(false);
	s_suspendReq = true;
}

void escTaskResume() {
	s_suspendReq = false;
}

bool escTaskSuspended() { return s_suspended; }
/** @} */

void escSnapshot(EscTelemetry *out) {
	critical_section_enter_blocking(&s_cs);
	*out = s_tel;
	critical_section_exit(&s_cs);
}

void escTaskBegin() {
	critical_section_init(&s_cs);
	memset((void *)&s_tel, 0, sizeof(s_tel));

	s_esc = new BidirDShotX1(DSHOT_PIN, DSHOT_SPEED_KBAUD);
	s_tel.initError = s_esc->initError();

#if KISS_TELEM_ENABLE
	memset(&s_kiss, 0, sizeof(s_kiss));
	// RX only: the ESC talks, we never answer. Claiming a TX pin would drive a
	// line the ESC is already driving.
	uart_init(KISS_UART, KISS_BAUD);
	gpio_set_function(KISS_TELEM_PIN, GPIO_FUNC_UART);
	uart_set_format(KISS_UART, 8, 1, UART_PARITY_NONE);
	uart_set_fifo_enabled(KISS_UART, true);
#endif

	s_nextSendUs = time_us_32();
	s_lastRateMs = millis();
}

/**
 * @brief Fold one decoded telemetry frame into @ref s_tel.
 *
 * Unit conversions follow the library's documented encodings: voltage arrives
 * in 250 mV steps, current in 1 A steps, temperature in whole degrees Celsius.
 *
 * @param type  Frame type reported by the library.
 * @param value Decoded payload, meaningless for the error types.
 */
static void applyTelemetry(BidirDshotTelemetryType type, uint32_t value) {
	uint8_t poles = s_poles;

	critical_section_enter_blocking(&s_cs);
	switch (type) {
		case BidirDshotTelemetryType::ERPM:
			s_tel.erpm = value;
			s_tel.rpm  = (poles >= 2) ? (value / (poles / 2)) : value;
			s_tel.lastRpmMs = millis();
			s_tel.goodPackets++;
			break;
		case BidirDshotTelemetryType::VOLTAGE:
			s_tel.volts = value * 0.25f;
			s_tel.haveVolts = true;
			s_tel.goodPackets++;
			break;
		case BidirDshotTelemetryType::CURRENT:
			s_tel.amps = (float)value;
			s_tel.haveAmps = true;
			s_tel.goodPackets++;
			break;
		case BidirDshotTelemetryType::TEMPERATURE:
			s_tel.tempC = (int16_t)value;
			s_tel.haveTemp = true;
			s_tel.goodPackets++;
			break;
		case BidirDshotTelemetryType::STRESS:
			s_tel.stress = (uint8_t)value;
			s_tel.haveStress = true;
			s_tel.goodPackets++;
			break;
		case BidirDshotTelemetryType::STATUS:
			s_tel.statusRaw = (uint8_t)value;
			s_tel.maxStress = (uint8_t)(value & ESC_STATUS_MAX_STRESS_MASK);
			s_tel.error     = (value & ESC_STATUS_ERROR_MASK)   != 0;
			s_tel.warning   = (value & ESC_STATUS_WARNING_MASK) != 0;
			s_tel.alert     = (value & ESC_STATUS_ALERT_MASK)   != 0;
			s_tel.goodPackets++;
			break;
		case BidirDshotTelemetryType::DEBUG_FRAME_1:
		case BidirDshotTelemetryType::DEBUG_FRAME_2:
		case BidirDshotTelemetryType::OTHER_VALUE:
			s_tel.goodPackets++;
			break;
		case BidirDshotTelemetryType::CHECKSUM_ERROR:
			s_tel.badPackets++;
			break;
		case BidirDshotTelemetryType::NO_PACKET:
			s_tel.noPackets++;
			break;
	}
	critical_section_exit(&s_cs);
}

void escTaskPoll() {
	// Handle a pin handover request before anything touches the driver. Both
	// the teardown and the rebuild happen here so the PIO state machine is
	// released and re-claimed by the core that owns it.
	if (s_suspendReq && s_esc) {
		delete s_esc;
		s_esc = nullptr;
#if KISS_TELEM_ENABLE
		// The AM32 bootloader owns the signal pin from here. Nothing will be
		// requesting telemetry, so a UART left running would only accumulate
		// noise into a decoder that has no way to tell it from a reply.
		uart_deinit(KISS_UART);
		s_kissPending = false;
#endif
		s_suspended = true;
		return;
	}
	if (!s_suspendReq && !s_esc) {
		s_esc = new BidirDShotX1(DSHOT_PIN, DSHOT_SPEED_KBAUD);
		s_armed = false;
		s_throttle = 0;
		s_edtAutoDone = false;
		s_nextSendUs = time_us_32();
#if KISS_TELEM_ENABLE
		memset(&s_kiss, 0, sizeof(s_kiss));
		s_kissCountdown = 0;
		s_kissPending = false;
		uart_init(KISS_UART, KISS_BAUD);
		gpio_set_function(KISS_TELEM_PIN, GPIO_FUNC_UART);
		uart_set_format(KISS_UART, 8, 1, UART_PARITY_NONE);
		uart_set_fifo_enabled(KISS_UART, true);
#endif
		critical_section_enter_blocking(&s_cs);
		s_tel.initError = s_esc->initError();
		critical_section_exit(&s_cs);
		s_suspended = false;
	}
	if (!s_esc) return;

	uint32_t now = time_us_32();
	if ((int32_t)(now - s_nextSendUs) < 0) return;
	s_nextSendUs += DSHOT_PERIOD_US;
	// if we fell badly behind (debugger stop, etc.) resync rather than burst
	if ((int32_t)(time_us_32() - s_nextSendUs) > (int32_t)(DSHOT_PERIOD_US * 4)) {
		s_nextSendUs = time_us_32() + DSHOT_PERIOD_US;
	}

	// 1. Drain the reply to the *previous* frame before sending the next one.
	uint32_t value = 0;
	BidirDshotTelemetryType type = s_esc->getTelemetryPacket(&value);
	applyTelemetry(type, value);

	// 2. Decide what to send.
	uint32_t ms = millis();
	bool uiAlive = (uint32_t)(ms - s_heartbeatMs) < UI_HEARTBEAT_TIMEOUT_MS;

	// Once the ESC has been fed idle frames for a moment, turn EDT on.
	if (!s_edtAutoDone && ms > 1500) {
		s_edtAutoDone = true;
		s_pendingCmd  = DSHOT_CMD_EXTENDED_TELEMETRY_ENABLE;
		s_pendingReps = 10;
		s_edtRequested = true;
	}

#if KISS_TELEM_ENABLE
	// Drain before sending, so a reply to the previous request is banked before
	// this frame potentially asks for another one.
	kissDrain(ms);

	// Commands own the frame when they are queued -- sendRaw11Bit() forces the
	// telemetry bit set anyway, and a reply arriving mid-command sequence would
	// just be noise.
	bool wantKiss = false;
	if (s_pendingReps == 0) {
		if (s_kissCountdown) {
			s_kissCountdown--;
		} else {
			wantKiss = true;
			s_kissCountdown = KISS_REQUEST_EVERY_N - 1;
		}
	}
#else
	const bool wantKiss = false;
#endif

	if (s_pendingReps > 0 && !s_armed) {
		s_esc->sendRaw11Bit(s_pendingCmd);
		s_pendingReps--;
	} else {
		// Zero throttle whenever disarmed or the UI has stopped talking to us.
		uint16_t t = (!s_armed || !uiAlive) ? 0 : s_throttle;
		// Built by hand rather than via sendThrottle(), which cannot set the
		// telemetry-request bit. @see kissBuildDshotPayload()
		s_esc->sendRaw12Bit(kissBuildDshotPayload(t, wantKiss));
	}

#if KISS_TELEM_ENABLE
	if (wantKiss) {
		// Align the assembler to this request: whatever is in the buffer is a
		// stalled partial frame, and the next ten bytes are the reply.
		kissExpectFrame(&s_kiss);
		s_kissPending = true;
		s_kissReqMs = ms;
	}
#endif

	// 3. Once a second, roll up a link-quality figure.
	if ((uint32_t)(ms - s_lastRateMs) >= 1000) {
		s_lastRateMs = ms;
		critical_section_enter_blocking(&s_cs);
		uint32_t good = s_tel.goodPackets - s_rateGoodMark;
		uint32_t bad  = s_tel.badPackets  - s_rateBadMark;
		s_rateGoodMark = s_tel.goodPackets;
		s_rateBadMark  = s_tel.badPackets;
		s_tel.packetRate = (uint16_t)(good > 65535 ? 65535 : good);
		uint32_t total = good + bad;
		s_tel.errPercent = total ? (uint8_t)((bad * 100) / total) : 0;
		critical_section_exit(&s_cs);
	}
}
