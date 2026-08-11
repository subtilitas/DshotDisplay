/**
 * @file am32_bl.cpp
 * @brief Bit-banged one-wire serial and the AM32 bootloader command sequences.
 *
 * 19200 baud is slow enough that a software UART is comfortably accurate on a
 * 150 MHz core: one bit is ~52 us, so even a few microseconds of jitter is well
 * inside the sampling window. Doing it in software rather than PIO also keeps
 * the PIO blocks free, and avoids fighting the DShot driver for them.
 *
 * Interrupts are masked per byte, not per frame. Masking for a whole frame
 * would starve the rest of the system for milliseconds; masking per byte keeps
 * the critical section to about half a millisecond.
 */

#include "am32_bl.h"
#include "am32_eeprom.h"
#include "config.h"

#include "plat.h"
#include <hardware/gpio.h>
#include <hardware/sync.h>
#include <string.h>

/** @brief How long to wait for the ACK that ends a command. */
#define AM32_ACK_TMO_MS  250

/** @brief How long to wait for any one byte once a reply has begun. */
#define AM32_BYTE_TMO_MS 50

static uint8_t  s_pin = 0xFF;                     /**< Signal GPIO, 0xFF when idle. */
static uint32_t s_baud = 19200;                   /**< Link rate. */
static uint32_t s_bitUs = 1000000UL / 19200;      /**< Derived bit period, us. */

void am32BlSetBaud(uint32_t baud) {
	if (!baud) return;
	s_baud = baud;
	s_bitUs = 1000000UL / baud;
}

uint32_t am32BlBaud() { return s_baud; }

/** @brief Bit period in microseconds, tracking @ref am32BlSetBaud. */
#define AM32_BIT_US (s_bitUs)

// ---------------------------------------------------------------------------
// CRC
// ---------------------------------------------------------------------------
uint16_t am32Crc16(const uint8_t *data, uint16_t len) {
	uint16_t crc = 0;
	for (uint16_t i = 0; i < len; i++) {
		uint8_t b = data[i];
		for (uint8_t bit = 0; bit < 8; bit++) {
			if ((b ^ (uint8_t)crc) & 1) {
				crc >>= 1;
				crc ^= 0xA001;
			} else {
				crc >>= 1;
			}
			b >>= 1;
		}
	}
	return crc;
}

// ---------------------------------------------------------------------------
// one-wire bit banging
// ---------------------------------------------------------------------------

/** @brief Busy-wait until @p deadline, which is a time_us_32() value. */
static inline void waitUntil(uint32_t deadline) {
	while ((int32_t)(time_us_32() - deadline) < 0) tight_loop_contents();
}

/** @brief Let go of the line so the ESC (or the pull-up) can drive it. */
static inline void lineRelease() {
	gpio_set_dir(s_pin, GPIO_IN);
	gpio_pull_up(s_pin);
}

/** @brief Take the line for transmitting. */
static inline void lineDrive() {
#if AM32_PUSH_PULL_TX
	gpio_put(s_pin, 1);
	gpio_set_dir(s_pin, GPIO_OUT);
#else
	gpio_pull_up(s_pin);          // idle high comes from the pull-up
	gpio_set_dir(s_pin, GPIO_IN);
#endif
}

/**
 * @brief Drive one bit level.
 *
 * Open-drain only ever pulls down and lets the pull-up supply the high level,
 * so the ESC can never be fighting a driven high if both talk at once.
 */
static inline void lineSet(bool high) {
#if AM32_PUSH_PULL_TX
	gpio_put(s_pin, high);
#else
	if (high) {
		gpio_set_dir(s_pin, GPIO_IN);
	} else {
		gpio_put(s_pin, 0);
		gpio_set_dir(s_pin, GPIO_OUT);
	}
#endif
}

/** @brief Transmit one byte, LSB first, 8N1. */
static void txByte(uint8_t b) {
	uint32_t irq = save_and_disable_interrupts();
	uint32_t t = time_us_32();

	lineSet(false);                           // start bit
	t += AM32_BIT_US; waitUntil(t);
	for (int i = 0; i < 8; i++) {
		lineSet((b >> i) & 1);
		t += AM32_BIT_US; waitUntil(t);
	}
	lineSet(true);                            // stop bit
	t += AM32_BIT_US; waitUntil(t);

	restore_interrupts(irq);
}

/**
 * @brief Receive one byte.
 * @param[out] out       Received byte.
 * @param      timeoutMs How long to wait for a start bit.
 * @return true on success, false on timeout.
 */
static bool rxByte(uint8_t *out, uint32_t timeoutMs) {
	uint32_t deadline = millis() + timeoutMs;

	// The line must be idle high before a start bit means anything. Without
	// this, arriving while the line is still low from a previous bit reads as
	// an immediate start bit and every byte after it is framed wrongly.
	while (!gpio_get(s_pin)) {
		if ((int32_t)(millis() - deadline) >= 0) return false;
		tight_loop_contents();
	}

	// Wait for the falling edge that begins a start bit.
	while (gpio_get(s_pin)) {
		if ((int32_t)(millis() - deadline) >= 0) return false;
		tight_loop_contents();
	}

	uint32_t irq = save_and_disable_interrupts();
	uint32_t t = time_us_32();

	// Sample in the middle of each bit rather than at its edge.
	t += AM32_BIT_US + AM32_BIT_US / 2;
	uint8_t b = 0;
	for (int i = 0; i < 8; i++) {
		waitUntil(t);
		if (gpio_get(s_pin)) b |= (uint8_t)(1 << i);
		t += AM32_BIT_US;
	}
	restore_interrupts(irq);

	*out = b;
	return true;
}

/** @brief Drive the line, send every byte, then release it for the reply. */
static void txBuf(const uint8_t *data, uint16_t len) {
	lineDrive();
	for (uint16_t i = 0; i < len; i++) txByte(data[i]);
	lineRelease();
}

/**
 * @brief Send a frame with its CRC appended, then read the reply.
 *
 * @param cmd      Bytes to send, CRC excluded.
 * @param cmdLen   Length of @p cmd.
 * @param[out] rx  Reply buffer, or nullptr to discard.
 * @param rxLen    Bytes of reply expected before the ACK.
 * @param checkCrc Whether the reply carries a CRC to validate.
 */
static Am32Result txFrame(const uint8_t *cmd, uint16_t cmdLen,
                          uint8_t *rx, uint16_t rxLen, bool checkCrc) {
	if (s_pin == 0xFF) return AM32_ERR_BUSY;

	uint8_t frame[300];
	if (cmdLen + 2 > (uint16_t)sizeof(frame)) return AM32_ERR_BUSY;
	memcpy(frame, cmd, cmdLen);
	uint16_t crc = am32Crc16(cmd, cmdLen);
	frame[cmdLen]     = (uint8_t)(crc & 0xFF);
	frame[cmdLen + 1] = (uint8_t)(crc >> 8);

	txBuf(frame, (uint16_t)(cmdLen + 2));

	// No echo to discard. A hardware one-wire UART receives its own
	// transmission and the host has to throw it away, but this is bit-banged:
	// the pin is an output while transmitting and is never sampled, so nothing
	// is captured. Reading "the echo" here would consume the ESC's real reply.

	uint8_t body[264];
	uint16_t bodyLen = rxLen;
	for (uint16_t i = 0; i < bodyLen; i++) {
		if (!rxByte(&body[i], AM32_BYTE_TMO_MS)) return AM32_ERR_TIMEOUT;
	}

	if (checkCrc && bodyLen) {
		uint8_t lo, hi;
		if (!rxByte(&lo, AM32_BYTE_TMO_MS)) return AM32_ERR_TIMEOUT;
		if (!rxByte(&hi, AM32_BYTE_TMO_MS)) return AM32_ERR_TIMEOUT;
		uint16_t want = (uint16_t)lo | ((uint16_t)hi << 8);
		if (am32Crc16(body, bodyLen) != want) return AM32_ERR_CRC;
	}

	uint8_t ack;
	if (!rxByte(&ack, AM32_ACK_TMO_MS)) return AM32_ERR_TIMEOUT;
	if (ack != AM32_ACK_OK) return AM32_ERR_NAK;

	if (rx && rxLen) memcpy(rx, body, rxLen);
	return AM32_OK;
}

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------
void am32BlBegin(uint8_t pin) {
	s_pin = pin;
	gpio_init(pin);
	lineRelease();
	delay(2);
}

void am32BlEnd() {
	if (s_pin != 0xFF) {
		gpio_set_dir(s_pin, GPIO_IN);
		gpio_disable_pulls(s_pin);
	}
	s_pin = 0xFF;
}

/**
 * @brief Last handshake reply, kept for on-screen diagnostics.
 *
 * Without a scope on the signal wire, knowing whether *anything* came back is
 * the difference between "the ESC never answered" and "it answered and we
 * framed it wrongly" -- two problems with opposite fixes.
 */
static uint8_t  s_lastRx[16];
static uint8_t  s_lastRxLen = 0;                  /**< Valid bytes in s_lastRx. */

/**
 * @brief Identity established by the handshake.
 *
 * The settings page lives at a different address on each MCU family, so none
 * of this can be assumed -- it has to come from the ESC.
 */
static uint8_t  s_escType = 0;                    /**< @see Am32EscType */
static uint16_t s_eepromAddr = 0;                 /**< Settings page address. */
static bool     s_addrDiv4 = false;               /**< Firmware addresses need >>2. */

uint16_t am32BlEepromAddr() { return s_eepromAddr; }
uint8_t  am32BlEscType()    { return s_escType; }
bool     am32BlAddrDiv4()   { return s_addrDiv4; }

const char *am32BlEscTypeName() {
	switch (s_escType) {
		case AM32_ESC_G071_2KB: return "G071";
		case AM32_ESC_F051_1KB: return "F051";
		case AM32_ESC_F3_2KB:   return "F3";
	}
	return "?";
}

uint8_t am32BlLastRx(const uint8_t **buf) { *buf = s_lastRx; return s_lastRxLen; }


void am32BlJumpToBootloader() {
	if (s_pin == 0xFF) return;

	// Running AM32 firmware watches its input pin: hold it low long enough and
	// it gives up on finding a valid signal and jumps to the bootloader. This
	// is what lets a configurator reach an ESC that is already powered, rather
	// than having to catch the brief window right after power-up.
	lineDrive();
	lineSet(false);
	delay(AM32_BOOT_LOW_MS);
	lineSet(true);
	lineRelease();

	// Give the firmware time to actually make the jump and the bootloader time
	// to initialise its pin before anything is sent at it.
	delay(AM32_BOOT_SETTLE_MS);
}

Am32Result am32BlHandshake(uint8_t *deviceInfo) {
	if (s_pin == 0xFF) return AM32_ERR_BUSY;

	s_lastRxLen = 0;

	// ESC_INIT_STRING, verbatim from a known-working configurator: twelve zero
	// bytes, 0x0D, "BLHeli", then the fixed signature F4 7D. 21 bytes total.
	//
	// The trailing F4 7D is a literal, not a checksum -- CRC-16/ARC over the
	// preceding 19 bytes is not 0x7DF4.
	static const uint8_t GREET[21] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x0D, 'B', 'L', 'H', 'e', 'l', 'i',
		0xF4, 0x7D
	};

	txBuf(GREET, sizeof(GREET));

	// Read whatever arrives rather than demanding an exact count, then index
	// backwards from the end. Counting from the end is echo-agnostic: a host
	// with a hardware UART on the shared wire also captures its own
	// transmission, and both cases still put the ACK last.
	uint8_t reply[40];
	int got = 0;
	while (got < (int)sizeof(reply)) {
		// Short timeout on the first byte so a miss costs little and we can
		// retry quickly; generous once the ESC has started talking.
		uint32_t tmo = got ? AM32_BYTE_TMO_MS : AM32_GREET_TMO_MS;
		if (!rxByte(&reply[got], tmo)) break;
		if (got < (int)sizeof(s_lastRx)) s_lastRx[got] = reply[got];
		got++;
	}
	s_lastRxLen = (uint8_t)(got > (int)sizeof(s_lastRx) ? sizeof(s_lastRx) : got);

	if (got == 0) return AM32_ERR_TIMEOUT;
	if (got < 5)  return AM32_ERR_NAK;   // replied, but too short to identify

	if (reply[got - 1] != AM32_ACK_OK) return AM32_ERR_NAK;

	// Fifth byte from the end identifies the MCU family, which is what decides
	// where its settings live. Getting this wrong reads the wrong flash page.
	s_escType = reply[got - 5];
	switch (s_escType) {
		case AM32_ESC_G071_2KB: s_eepromAddr = 0x7E00; s_addrDiv4 = true;  break;
		case AM32_ESC_F051_1KB: s_eepromAddr = 0x7C00; s_addrDiv4 = false; break;
		case AM32_ESC_F3_2KB:   s_eepromAddr = 0xF800; s_addrDiv4 = false; break;
		default:
			s_eepromAddr = 0;
			s_addrDiv4 = false;
			return AM32_ERR_NAK;   // answered, but not a family we know
	}

	if (deviceInfo) {
		int from = got - 9 < 0 ? 0 : got - 9;
		memcpy(deviceInfo, &reply[from], 9);
	}
	return AM32_OK;
}

/** @brief Point the bootloader at a flash address. */
static Am32Result setAddress(uint16_t addr) {
	const uint8_t cmd[4] = {AM32_CMD_SET_ADDRESS, 0x00,
	                        (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF)};
	Am32Result r = txFrame(cmd, sizeof(cmd), nullptr, 0, false);
	delay(AM32_CMD_GAP_MS);
	return r;
}

Am32Result am32BlRead(uint16_t addr, uint8_t *buf, uint8_t len) {
	Am32Result r = setAddress(addr);
	if (r != AM32_OK) return r;
	delay(AM32_EEPROM_GAP_MS);

	// The wire encodes 256 as 0, which a uint8_t length already gives us for
	// free -- passing 0 here asks for a full 256-byte read.
	const uint8_t cmd[2] = {AM32_CMD_READ_FLASH, len};
	return txFrame(cmd, sizeof(cmd), buf, len, true);
}

Am32Result am32BlWrite(uint16_t addr, const uint8_t *buf, uint8_t len) {
	Am32Result r = setAddress(addr);
	if (r != AM32_OK) return r;

	// SET_BUFFER is not acknowledged on its own; the ESC replies once the
	// payload that follows has landed. Asking for an ACK here would stall.
	const uint8_t sz[4] = {AM32_CMD_SET_BUFFER, 0x00, 0x00, len};
	uint8_t frame[8];
	memcpy(frame, sz, sizeof(sz));
	uint16_t c = am32Crc16(sz, sizeof(sz));
	frame[4] = (uint8_t)(c & 0xFF);
	frame[5] = (uint8_t)(c >> 8);
	txBuf(frame, 6);
	delay(AM32_CMD_GAP_MS);

	r = txFrame(buf, len, nullptr, 0, false);
	if (r != AM32_OK) return r;
	delay(AM32_EEPROM_GAP_MS);

	// PROG_FLASH is two bytes, 0x01 0x01 -- a bare 0x01 is not accepted.
	const uint8_t prog[2] = {AM32_CMD_PROG_FLASH, 0x01};
	return txFrame(prog, sizeof(prog), nullptr, 0, false);
}

Am32Result am32BlErasePage(uint16_t addr) {
	Am32Result r = setAddress(addr);
	if (r != AM32_OK) return r;
	const uint8_t cmd[1] = {AM32_CMD_ERASE_FLASH};
	return txFrame(cmd, sizeof(cmd), nullptr, 0, false);
}

Am32Result am32BlRun() {
	const uint8_t cmd[1] = {AM32_CMD_RUN};
	return txFrame(cmd, sizeof(cmd), nullptr, 0, false);
}

const char *am32ResultText(Am32Result r) {
	switch (r) {
		case AM32_OK:          return "OK";
		case AM32_ERR_TIMEOUT: return "NO REPLY";
		case AM32_ERR_NAK:     return "REFUSED";
		case AM32_ERR_CRC:     return "CRC FAIL";
		case AM32_ERR_BUSY:    return "NOT READY";
	}
	return "?";
}
