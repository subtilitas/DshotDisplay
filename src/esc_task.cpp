#include "esc_task.h"
#include "config.h"
#include "esc_merge.h"
#include "kiss_telem.h"
#include "pio_uart_rx.h"
#include "settings.h"

#include "plat.h"
#include <string.h>
#include <PIO_DShot.h>
#include <pico/critical_section.h>
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

/**
 * @defgroup esc_wiring Live wiring (core0 writes, core1 latches on rebuild)
 * @brief What escTaskConfigure() last set. Read only while building the driver.
 *
 * Latched at construction rather than read per frame, so a pin change cannot
 * take effect halfway: the driver is destroyed, these are read once, and the
 * new driver is built from a consistent set.
 * @{
 */
// Seeded properly by escTaskInit() from the stored settings. These initialisers
// only cover the window before that runs, which is why they are the safest
// values rather than any particular board's: pin 0 drives nothing on either
// board until escTaskInit() replaces it.
static volatile uint8_t  s_dshotPin   = 0;                      /**< ESC signal GPIO. */
static volatile uint16_t s_dshotKbaud = DSHOT_SPEED_KBAUD;      /**< DShot bitrate. */
static volatile uint8_t  s_kissEnable = 0;                      /**< KISS wire expected. */
static volatile uint8_t  s_kissPin    = 0;                      /**< KISS RX GPIO. */
static volatile bool     s_rebuildReq = false;                  /**< Rebuild pending. */
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
static bool     s_edtTried = false;    /**< An enable has gone to this ESC. */
static uint32_t s_edtLastTryMs = 0;    /**< millis() when it did. */
static bool     s_edtArmedWas = false; /**< Arm state the last attempt saw. */
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
 * @brief True while a PIO receiver is running on the KISS pin.
 *
 * The receiver used to be one of the two hardware UARTs, picked from the pin,
 * which meant the pin had to be one of the eight the RP2350 can receive on —
 * and on the 2.8" board exactly one of those is free and it is the one the ESC
 * signal wire is on. @see pio_uart_rx.h for what replaced it and why.
 */
static bool s_kissRx = false;

/**
 * @brief Start receiving KISS on @p pin.
 *
 * Receive only: the ESC talks and we never answer, and driving a line the ESC
 * is already driving is not a thing to do by accident.
 *
 * A refusal — every state machine already claimed — leaves @ref s_kissRx false,
 * and the effect is the same as an unplugged telemetry wire: no KISS frames,
 * the merged reading falls back to EDT, and the tiles say so. That is the right
 * failure for something that cannot happen on a board this firmware builds for
 * (two programs resident, twelve state machines) and must still not fault.
 *
 * @param pin GPIO to receive on. Any of them.
 */
static void kissUartBegin(uint8_t pin) {
	memset(&s_kiss, 0, sizeof(s_kiss));
	s_kissCountdown = 0;
	s_kissPending = false;
	s_kissRx = pioUartRxBegin(pin, KISS_BAUD);
}

/** @brief Release the KISS receiver and its pin, if one is running. */
static void kissUartEnd() {
	if (!s_kissRx) return;
	pioUartRxEnd();
	s_kissRx = false;
	s_kissPending = false;
}

/**
 * @brief Drain the receiver and fold any complete frame into @ref s_tel.
 *
 * Called from escTaskPoll() on core1, on every pass rather than once per frame
 * slot. Bounded work either way — the FIFO is eight bytes deep and this never
 * blocks — but eight bytes is 700 us at 115200 baud, against the 1 ms between
 * DShot frames, so draining at the frame rate would lose the tail of most
 * replies. The PL011 this replaced held 32 and could afford to wait.
 *
 * Doing it here at all, rather than handing the receiver to core0, is the same
 * argument as before: it is a FIFO read and a byte of state machine, and core1
 * has the cycles between frames.
 */
static void kissDrain() {
	KissFrame f;
	if (!s_kissRx) return;

	while (pioUartRxReadable()) {
		uint8_t c = pioUartRxGetc();
		if (kissFeed(&s_kiss, c, &f)) {
			s_kissPending = false;
			critical_section_enter_blocking(&s_cs);
			s_tel.kissVolts  = f.volts;
			s_tel.kissAmps   = f.amps;
			s_tel.kissTempC  = f.tempC;
			s_tel.kissMah    = f.mah;
			s_tel.kissErpm   = f.erpm;
			s_tel.kissLastMs = millis();
			s_tel.kissGood   = s_kiss.good;
			s_tel.haveKiss   = true;
			critical_section_exit(&s_cs);
		}
	}

	// A reply that never completed. Count it and drop the partial frame, or the
	// next request's bytes would append to it and decode at the wrong offset.
	if (s_kissPending && (uint32_t)(millis() - s_kissReqMs) > KISS_REPLY_TIMEOUT_MS) {
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

bool escRequestBeep(uint8_t n) {
	if (s_armed) return false;
	if (n < 1) n = 1;
	if (n > 5) n = 5;
	s_pendingCmd  = (uint8_t)(DSHOT_CMD_BEACON1 + (n - 1));
	s_pendingReps = 6;
	return true;
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

void escTaskInit() {
	// Core0 owns this, and must do it before core1 is launched. See the note in
	// esc_task.h: core0's first uiTick() calls escSnapshot(), which enters this
	// critical section, and it will get there long before core1 has booted.
	critical_section_init(&s_cs);
	memset((void *)&s_tel, 0, sizeof(s_tel));

	// Seed the wiring from the stored settings, so core1's very first build uses
	// the user's pins rather than the compiled defaults followed by a rebuild.
	const Settings *cfg = settings();
	s_dshotPin   = cfg->dshotPin;
	s_dshotKbaud = cfg->dshotKbaud;
	s_kissEnable = cfg->kissEnable;
	s_kissPin    = cfg->kissPin;
	s_rebuildReq = false;
}

void escTaskConfigure(uint8_t dshotPin, uint16_t dshotKbaud,
                      bool kissEnable, uint8_t kissPin) {
	if (s_dshotPin == dshotPin && s_dshotKbaud == dshotKbaud &&
	    s_kissEnable == (kissEnable ? 1u : 0u) && s_kissPin == kissPin)
		return;

	// Disarm first, and through escSetArmed() so the throttle is zeroed too. The
	// ESC on the old pin is about to stop hearing frames; leaving a non-zero
	// throttle latched for whatever gets built next is not a state to pass
	// through.
	escSetArmed(false);
	s_dshotPin   = dshotPin;
	s_dshotKbaud = dshotKbaud;
	s_kissEnable = kissEnable ? 1u : 0u;
	s_kissPin    = kissPin;
	s_rebuildReq = true;
}

uint8_t escTaskDshotPin() { return s_dshotPin; }

void escTaskBegin() {
	// Nothing is claimed here. The driver and the UART are built by the first
	// escTaskPoll(), which is also the path a suspend or a reconfigure returns
	// through -- one construction site, so the two cannot drift apart.
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
			s_tel.edtVoltsMs = millis();
			s_tel.goodPackets++;
			break;
		case BidirDshotTelemetryType::CURRENT:
			s_tel.amps = (float)value;
			s_tel.edtAmpsMs = millis();
			s_tel.goodPackets++;
			break;
		case BidirDshotTelemetryType::TEMPERATURE:
			s_tel.tempC = (int16_t)value;
			s_tel.edtTempMs = millis();
			s_tel.goodPackets++;
			break;
		case BidirDshotTelemetryType::STRESS:
			s_tel.stress = (uint8_t)value;
			s_tel.edtStressMs = millis();
			s_tel.goodPackets++;
			break;
		case BidirDshotTelemetryType::STATUS:
			s_tel.statusRaw = (uint8_t)value;
			s_tel.maxStress = (uint8_t)(value & ESC_STATUS_MAX_STRESS_MASK);
			s_tel.error     = (value & ESC_STATUS_ERROR_MASK)   != 0;
			s_tel.warning   = (value & ESC_STATUS_WARNING_MASK) != 0;
			s_tel.alert     = (value & ESC_STATUS_ALERT_MASK)   != 0;
			s_tel.edtStatusMs = millis();
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
	// Handle a handover or a reconfigure before anything touches the driver.
	// Teardown and rebuild both happen here so the PIO state machine is released
	// and re-claimed by the core that owns it.
	//
	// The teardown runs on the *request* rather than on `s_esc` being non-null.
	// Gating it on the driver existing left a hole: a suspend arriving before
	// core1 had ever built one satisfied neither branch, so `s_suspended` never
	// became true and the AM32 screen waited on a handover that could not
	// complete.
	if (s_suspendReq || s_rebuildReq) {
		if (s_esc) {
			delete s_esc;
			s_esc = nullptr;
		}
#if KISS_TELEM_ENABLE
		// Whoever owns the pin next, it is not us. A UART left running would
		// accumulate noise into a decoder with no way to tell it from a reply.
		kissUartEnd();
#endif
		s_rebuildReq = false;
		if (s_suspendReq) {
			s_suspended = true;
			return;
		}
	}
	if (!s_suspendReq && !s_esc) {
		// Latch the wiring once, here, so a pin change cannot be observed
		// half-applied. @see esc_wiring
		uint8_t  pin   = s_dshotPin;
		uint16_t kbaud = s_dshotKbaud;
		s_esc = new BidirDShotX1(pin, kbaud);
		s_armed = false;
		s_throttle = 0;
		s_edtTried = false;
		s_nextSendUs = time_us_32();
#if KISS_TELEM_ENABLE
		if (s_kissEnable && s_kissPin != pin) kissUartBegin(s_kissPin);
#endif
		critical_section_enter_blocking(&s_cs);
		s_tel.initError = s_esc->initError();
		critical_section_exit(&s_cs);
		s_suspended = false;
	}
	if (!s_esc) return;

#if KISS_TELEM_ENABLE
	// Ahead of the frame deadline, not after it: this loop runs far faster than
	// the 1 kHz frame rate, and the receiver only holds eight bytes.
	// @see kissDrain()
	kissDrain();
#endif

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

	// Keep asking for EDT while an ESC is answering and EDT is not arriving.
	// Every timestamp read here is written by applyTelemetry() on this core, so
	// none of it needs the lock. See edtAutoAction() for the rule and for the
	// two one-shot versions that came before it.
	bool linkUp = s_tel.lastRpmMs != 0 &&
	              (uint32_t)(ms - s_tel.lastRpmMs) < ESC_LINK_STALE_MS;

	// The success condition is the same fact the header chip shows, computed
	// the same way: the retry stops exactly when the display says EDT ON, and
	// starts again if that ever goes back to OFF with an ESC still connected.
	bool edtFresh = escFieldFresh(s_tel.edtVoltsMs,  ms, EDT_STALE_MS) ||
	                escFieldFresh(s_tel.edtAmpsMs,   ms, EDT_STALE_MS) ||
	                escFieldFresh(s_tel.edtTempMs,   ms, EDT_STALE_MS) ||
	                escFieldFresh(s_tel.edtStressMs, ms, EDT_STALE_MS) ||
	                escFieldFresh(s_tel.edtStatusMs, ms, EDT_STALE_MS);

	// Arming or disarming is a reason to ask now rather than at the next
	// interval. Commands only go out while disarmed, so a disarm is the first
	// chance an attempt deferred by `s_armed` below has had, and making it wait
	// out the interval on top of that is time with the tiles blank for nothing.
	if (s_armed != s_edtArmedWas) {
		s_edtArmedWas = s_armed;
		s_edtTried = false;
	}

	switch (edtAutoAction(linkUp, edtFresh, s_edtTried,
	                      ms - s_edtLastTryMs, EDT_RETRY_MS)) {
		case EdtAutoAction::Send:
			// DShot commands only take while disarmed, so if the tester is
			// armed, leave the attempt unmade and come back next frame rather
			// than spending it on a frame that cannot carry it.
			if (s_armed) break;
			s_edtTried     = true;
			s_edtLastTryMs = ms;
			s_pendingCmd   = DSHOT_CMD_EXTENDED_TELEMETRY_ENABLE;
			s_pendingReps  = 10;
			s_edtRequested = true;
			break;
		case EdtAutoAction::Rearm:
			// The ESC is gone. Whatever replaces it is a different ESC and
			// starts from nothing having been sent to it.
			s_edtTried     = false;
			s_edtRequested = false;
			break;
		case EdtAutoAction::None:
			break;
	}

#if KISS_TELEM_ENABLE
	// Commands own the frame when they are queued -- sendRaw11Bit() forces the
	// telemetry bit set anyway, and a reply arriving mid-command sequence would
	// just be noise.
	bool wantKiss = false;
	if (s_kissRx && s_pendingReps == 0) {
		if (s_kissCountdown) {
			s_kissCountdown--;
		} else {
			wantKiss = true;
			s_kissCountdown = KISS_REQUEST_EVERY_N - 1;
		}
	}
#else
	// Not const: escFrameAction() decides the final value below, and this branch
	// still has to hold it even though nothing reads it afterwards.
	bool wantKiss = false;
#endif

	// The decision itself is escFrameAction(), which is pure and lives in the
	// header so the host suite can reach it. This function only does the I/O.
	EscFrame f = escFrameAction(s_armed, uiAlive, s_throttle,
	                            s_pendingCmd, s_pendingReps, wantKiss);
	if (f.sendCommand) {
		s_esc->sendRaw11Bit(f.command);
		s_pendingReps--;
	} else {
		// Built by hand rather than via sendThrottle(), which cannot set the
		// telemetry-request bit. @see kissBuildDshotPayload()
		s_esc->sendRaw12Bit(kissBuildDshotPayload(f.throttle, f.requestKiss));
	}
	wantKiss = f.requestKiss;

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
