/**
 * @file am32_bl.h
 * @brief AM32 / BLHeli-style bootloader transport over the ESC signal wire.
 *
 * The ESC signal pin is a single-wire half-duplex serial link at 19200 baud,
 * 8N1.
 *
 * @note A host using a hardware UART on this wire receives its own
 *       transmission and must discard the echo. This implementation bit-bangs
 *       and never samples the pin while driving it, so no echo is captured and
 *       none is discarded. Replies are indexed from the *end* of what arrives,
 *       which is correct either way.
 *
 * Protocol constants are those of a known-working AM32 configurator: the 21-byte
 * init string, the command encodings, and a CRC-16/ARC (polynomial 0xA001,
 * init 0) appended low byte first to every frame.
 *
 * @note Deliberately generic. Firmware flashing needs no new transport — it is
 *       the same setAddress / setBuffer / write sequence aimed at a different
 *       flash range, plus an erase.
 *
 * @warning This drives the same pin as the DShot output. escTaskSuspend() must
 *          be in effect before any call here, or the two will fight over the
 *          pin and the PIO state machine.
 */

#pragma once

#include <stdint.h>

/** @brief Bootloader command bytes. */
enum Am32BlCmd : uint8_t {
	AM32_CMD_RUN          = 0x00, /**< Leave the bootloader and start firmware. */
	AM32_CMD_PROG_FLASH   = 0x01, /**< Commit the staged buffer to flash. */
	AM32_CMD_ERASE_FLASH  = 0x02, /**< Erase the page at the current address. */
	AM32_CMD_READ_FLASH   = 0x03, /**< Read back from the current address. */
	AM32_CMD_SET_BUFFER   = 0xFE, /**< Stage a payload for the next write. */
	AM32_CMD_SET_ADDRESS  = 0xFF, /**< Set the flash address for what follows. */
};

/** @brief Byte the bootloader replies with on success. */
#define AM32_ACK_OK 0x30

/**
 * @brief MCU family, reported as the fifth byte from the end of the handshake.
 *
 * This decides where the settings page lives, so it has to be read from the
 * ESC rather than assumed.
 */
enum Am32EscType : uint8_t {
	AM32_ESC_G071_2KB = 0x2B, /**< STM32G071, 2 KB pages, settings at 0x7E00. */
	AM32_ESC_F051_1KB = 0x1F, /**< STM32F051, 1 KB pages, settings at 0x7C00. */
	AM32_ESC_F3_2KB   = 0x35, /**< STM32F3,   2 KB pages, settings at 0xF800. */
};

/** @brief Settings-page address for the connected ESC. 0 until identified. */
uint16_t am32BlEepromAddr();

/** @brief Raw MCU family byte from the handshake. @see Am32EscType */
uint8_t am32BlEscType();

/** @brief Short name of the detected family, for display. */
const char *am32BlEscTypeName();

/**
 * @brief Whether flash addresses must be shifted right by two for this part.
 * @note Applies to firmware images, not to the settings page.
 */
bool am32BlAddrDiv4();

/** @brief Outcome of a transport operation. */
enum Am32Result : uint8_t {
	AM32_OK = 0,      /**< Completed and acknowledged. */
	AM32_ERR_TIMEOUT, /**< No reply within the deadline. */
	AM32_ERR_NAK,     /**< Reply arrived but was not an ACK. */
	AM32_ERR_CRC,     /**< Reply failed its checksum. */
	AM32_ERR_BUSY,    /**< Transport not connected. */
};

/**
 * @brief CRC16 used by every bootloader frame.
 *
 * Reflected, polynomial 0xA001, initial value 0. Appended low byte first.
 *
 * @param data Bytes to checksum.
 * @param len  Number of bytes.
 * @return The 16-bit checksum.
 */
uint16_t am32Crc16(const uint8_t *data, uint16_t len);

/**
 * @brief Claim the signal pin and prepare the one-wire link.
 * @param pin GPIO wired to the ESC signal line.
 */
void am32BlBegin(uint8_t pin);

/** @brief Release the pin so the DShot task can take it back. */
void am32BlEnd();

/**
 * @brief Attempt to force a running ESC into its bootloader by holding low.
 *
 * @warning Disabled by default (@ref AM32_FORCE_LOW_JUMP) and kept only as an
 *          escape hatch. It did not work on the hardware this was developed
 *          against, and holding the line low makes the host deaf for the
 *          duration — which costs more power-up windows than it forces. The
 *          reliable route is to repeat the init string and power-cycle the ESC.
 */
void am32BlJumpToBootloader();

/**
 * @brief Try once to get the bootloader's attention.
 *
 * The bootloader listens only briefly after the ESC powers up, so this is meant
 * to be called repeatedly while the user power-cycles. It identifies the MCU
 * family on success, which is what makes am32BlEepromAddr() valid.
 *
 * @param[out] deviceInfo Receives 9 bytes of device information on success.
 *                        May be nullptr.
 * @return AM32_OK once the bootloader answers and its family is recognised.
 *         AM32_ERR_TIMEOUT for silence, which is the normal state between
 *         power-ups; AM32_ERR_NAK if bytes arrived but did not parse, which is
 *         a meaningfully different failure worth surfacing.
 */
Am32Result am32BlHandshake(uint8_t *deviceInfo);

/**
 * @brief Bytes captured during the last handshake, for on-screen diagnostics.
 *
 * Distinguishes "the ESC never jumped" (nothing) from "it replied and we
 * decoded it wrongly" (something, but not a valid info block).
 *
 * @param[out] buf Receives a pointer to the capture buffer.
 * @return Number of valid bytes, 0..16.
 */
uint8_t am32BlLastRx(const uint8_t **buf);

/**
 * @brief Change the bit rate. The link is 19200 8N1; this is here to override.
 * @param baud Bits per second. Ignored if zero.
 */
void am32BlSetBaud(uint32_t baud);

/** @brief Current bit rate. */
uint32_t am32BlBaud();

/**
 * @brief Read @p len bytes of ESC flash starting at @p addr.
 * @param addr Flash address; use am32BlEepromAddr() for the settings page.
 * @param buf  Destination.
 * @param len  Byte count. 0 requests a full 256 bytes.
 */
Am32Result am32BlRead(uint16_t addr, uint8_t *buf, uint8_t len);

/**
 * @brief Stage @p len bytes and commit them to flash at @p addr.
 *
 * @warning Writes to a live motor controller. The caller is responsible for
 *          confirming intent and for the ESC being disarmed.
 *
 * @param addr Flash address.
 * @param buf  Source data.
 * @param len  Byte count, 1..255.
 */
Am32Result am32BlWrite(uint16_t addr, const uint8_t *buf, uint8_t len);

/**
 * @brief Erase the flash page containing @p addr.
 * @note Not needed for settings writes; present for firmware flashing.
 */
Am32Result am32BlErasePage(uint16_t addr);

/** @brief Tell the bootloader to start the application firmware. */
Am32Result am32BlRun();

/** @brief Human-readable form of an @ref Am32Result. */
const char *am32ResultText(Am32Result r);

/**
 * @defgroup am32_raw Raw line access
 * @brief Byte-level access for the USB bridge, below the command layer.
 *
 * The bridge forwards traffic it does not interpret, so it needs the bit-banged
 * UART without the frame, CRC and ACK handling built on top of it.
 * @{
 */

/**
 * @brief Transmit bytes verbatim, then release the line for a reply.
 * @param data Bytes to send.
 * @param len  Number of bytes.
 */
void am32WriteRaw(const uint8_t *data, uint16_t len);

/** @brief Called with each byte the instant it has been clocked out. */
typedef void (*Am32ByteSink)(uint8_t b);

/**
 * @brief Transmit bytes, reporting each one as it goes out on the wire.
 *
 * Identical to am32WriteRaw() but for the callback, which exists so the USB
 * bridge can reproduce a real one-wire echo *at wire speed*.
 *
 * That pacing is not cosmetic. On a genuine one-wire link the host's receiver
 * sees its own transmission as it happens, and the reference configurator
 * depends on it:
 *
 * ```
 * time.sleep(0.025)
 * self.last_result = self.serial_port.read_all()
 * if len(self.last_result) > 1:
 *     if int(self.last_result[-1]) == 0x30: ...
 *     else: self.last_result = None        # discarded
 * ```
 *
 * A lone ACK fails `len > 1`, and a read that does not end in one is thrown
 * away. So the ACK has to arrive in the same 25 ms window as at least one other
 * byte. For a 256-byte payload -- 134 ms on the wire at 19200 baud -- what
 * satisfies that on real hardware is the *tail of the echo* still arriving when
 * the ESC acknowledges.
 *
 * Echo the whole frame at once instead and the tool sees a big burst that does
 * not end in 0x30 (discarded), then silence, then a solitary ACK it will not
 * accept. Settings survive it because the tool sleeps twice as long for those;
 * firmware writes cannot.
 *
 * @param data Bytes to send.
 * @param len  Number of bytes.
 * @param sink Called after each byte reaches the wire. May be nullptr.
 */
void am32WriteRawEchoed(const uint8_t *data, uint16_t len, Am32ByteSink sink);

/**
 * @brief Collect whatever arrives within a window.
 * @param[out] buf      Destination.
 * @param      maxLen   Capacity of @p buf.
 * @param      windowMs How long to wait for the first byte; subsequent bytes
 *                      are gathered until the line goes idle.
 * @return Number of bytes received.
 */
uint16_t am32ReadRaw(uint8_t *buf, uint16_t maxLen, uint32_t windowMs);

/** @} */
