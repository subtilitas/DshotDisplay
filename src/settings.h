/**
 * @file settings.h
 * @brief Settings the user can change on the board, persisted to flash.
 *
 * Everything here used to be a `#define` in @ref config.h, changeable only by
 * recompiling. Those values are still there and are still the defaults — what
 * changed is that they are now the *starting point* rather than the answer.
 *
 * @section settings_why Why these, and not the rest of config.h
 *
 * A setting earns a place here if getting it wrong is quiet. The ESC pin is the
 * canonical case: point it at a GPIO no wire is on and nothing errors, nothing
 * warns, and the ESC simply never hears a frame. That is not a value to require
 * a toolchain to change.
 *
 * Timings, buffer sizes and protocol constants stay in config.h. They fail
 * loudly, they are not things a user on a bench needs mid-session, and every
 * one of them added here is another field that has to survive a version bump.
 *
 * @section settings_storage Storage
 *
 * One dedicated flash sector, at the top of the address space the build
 * declares — see settings_flash.cpp. The block is versioned and CRC'd, and
 * anything that fails to validate falls back to compiled defaults *whole*,
 * never field by field. @see settingsValidate()
 *
 * Writing is deliberately manual. A flash erase parks core1 for tens of
 * milliseconds, which stops the DShot pump; that is a thing to do on purpose
 * with a button, not a side effect of turning a dial.
 */

#pragma once

#include <stdint.h>

#include "board_desc.h"

/** @brief Identifies a settings block. ASCII "DSHT". */
#define SETTINGS_MAGIC   0x44534854u

/**
 * @brief Layout version.
 *
 * Bump on any change to @ref Settings. A block with a different version is
 * discarded entirely rather than migrated — see @ref settingsLoad() for why
 * that is the safe direction here.
 */
#define SETTINGS_VERSION 2

/** @brief The persisted settings. */
struct Settings {
	/**
	 * @brief Which board this block was written on, one of @ref board_desc_ids.
	 *
	 * A record, never an input. The board is identified at boot — a unified
	 * image probes for its always-on I2C devices, see boardProbe(); a
	 * single-board image is told by the build — and @ref settingsValidate()
	 * overwrites this field with that answer. So a card moved to the other
	 * board arrives as *that* board's settings, every pin in it re-judged,
	 * instead of a pin map for hardware that is not there.
	 *
	 * Kept in the block precisely so that mismatch is visible rather than
	 * implied. It is not a choice, and nothing in the UI offers to change it.
	 */
	uint8_t  boardId;
	uint8_t  dshotPin;     /**< GPIO carrying the ESC signal wire. */
	uint16_t dshotKbaud;   /**< DShot bitrate: 150, 300, 600 or 1200. */
	uint8_t  kissEnable;   /**< Non-zero to claim a UART for KISS telemetry. */
	uint8_t  kissPin;      /**< GPIO the ESC's telemetry pad connects to. Any free one. */
	uint8_t  poles;        /**< Motor pole count, for eRPM to RPM. */
	uint16_t maxThrottle;  /**< Throttle ceiling, out of 0..2000. */
	uint8_t  backlight;    /**< Preferred backlight level, 0..255. */
	uint8_t  highContrast; /**< Non-zero selects @ref Theme::HighContrast. */
};

/**
 * @brief What a settings block looks like in flash.
 *
 * Size is recorded as well as version so a block written by a build with a
 * different struct layout is rejected even if someone forgets to bump
 * @ref SETTINGS_VERSION.
 */
struct SettingsBlock {
	uint32_t magic;    /**< @ref SETTINGS_MAGIC. */
	uint16_t version;  /**< @ref SETTINGS_VERSION. */
	uint16_t size;     /**< `sizeof(Settings)` when written. */
	Settings s;        /**< The payload. */
	uint32_t crc32;    /**< CRC-32 over every byte before this one. */
};

/**
 * @defgroup settings_pins Pin rules
 * @brief Which GPIOs a user is allowed to pick, and what they can be used for.
 *
 * Both are pure and host-tested, because the alternative to encoding them is a
 * picker that offers a pin the panel is already using.
 * @{
 */

/**
 * @brief True if @p pin is free on this board for an ESC or telemetry wire.
 *
 * Reads `BOARD_FREE_GPIO_MASK` from the board header. Everything not in that
 * mask drives an on-board peripheral, and assigning one is how you end up
 * sending DShot out of a backlight transistor.
 *
 * @param pin GPIO number.
 * @return true if the pin is unclaimed.
 */
bool settingsPinFree(uint8_t pin);

/**
 * @brief The next free GPIO after @p from, wrapping.
 *
 * Drives the `-`/`+` buttons on the setup screen. Stepping through only legal
 * values is what removes the error state entirely: there is no invalid pin to
 * reject, because one cannot be selected.
 *
 * @param from Current pin.
 * @param dir  +1 or -1.
 * @return The next candidate, or @p from if the board offers no other.
 */
uint8_t settingsNextPin(uint8_t from, int dir);

/**
 * @brief settingsPinFree(), judged against a specific board.
 *
 * The SETUP screen can hold a board choice that has not been applied yet — the
 * hardware only changes on reboot — and its pin rules must follow the choice,
 * not the live board. An id this image cannot drive falls back to the live
 * board's mask.
 *
 * @param boardIdArg One of @ref board_desc_ids.
 * @param pin        GPIO number.
 * @return true if the pin is unclaimed on that board.
 */
bool settingsPinFreeOn(uint8_t boardIdArg, uint8_t pin);

/**
 * @brief settingsNextPin(), stepping through a specific board's free pins.
 *
 * @param boardIdArg One of @ref board_desc_ids.
 * @param from       Current pin.
 * @param dir        +1 or -1.
 * @return The next candidate, or @p from if that board offers no other.
 */
uint8_t settingsNextPinOn(uint8_t boardIdArg, uint8_t from, int dir);

/** @} */

/**
 * @defgroup settings_api Access
 * @{
 */

/**
 * @brief Fill @p out with the compiled defaults from @ref config.h.
 * @param[out] out Destination.
 */
void settingsDefaults(Settings *out);

/**
 * @brief Clamp @p s into the range every consumer assumes.
 *
 * Called on load and before every save, so no code downstream has to defend
 * itself against a pole count of zero or a pin the panel is using.
 *
 * Repairs, in order: pole count even and in range; throttle ceiling a whole
 * number of steps and within @ref MAX_THROTTLE_CEILING; DShot bitrate one of
 * the four legal values; ESC pin free; KISS pin free, UART-capable, and not the
 * same pin as the ESC — KISS is switched off rather than moved if it is not.
 *
 * @param[in,out] s Settings to repair in place.
 * @return true if nothing needed changing.
 */
bool settingsValidate(Settings *s);

/**
 * @brief Load from storage, or fall back to defaults.
 *
 * A block that fails magic, size, version or CRC is discarded **whole**. Not
 * field by field: a partially-trusted block is how a corrupted byte becomes a
 * throttle ceiling of 100 %, and there is no version of that trade worth
 * taking. @ref settingsStored() reports which happened.
 */
void settingsLoad();

/**
 * @brief The live settings. Mutable; changes take effect immediately.
 * @return Pointer to the working copy.
 */
Settings *settings();

/**
 * @brief Write the live settings to flash.
 *
 * Validates first, so a save can only ever store something loadable.
 *
 * @warning Erasing flash parks core1 and stalls the DShot pump for tens of
 *          milliseconds. Callers must be disarmed. The setup screen enforces
 *          that; this function cannot, because it has no way to ask.
 *
 * @return true if the block was written and read back correctly.
 */
bool settingsSave();

/** @brief True if a valid block was found at boot. @return Load outcome. */
bool settingsStored();

/**
 * @brief True if the live settings differ from what is in flash.
 * @return Whether a save would change anything.
 */
bool settingsDirty();

/** @} */

/**
 * @defgroup settings_storage Storage back end
 * @brief The two calls that touch flash, kept behind a seam.
 *
 * settings.cpp holds the rules and is compiled into the host test suite;
 * settings_flash.cpp holds the RP2350 flash sequence and cannot be. The host
 * suite supplies its own RAM-backed pair, which is what lets the validation and
 * fallback rules above be tested at all.
 * @{
 */

/**
 * @brief Read the persisted block.
 * @param[out] dst Destination buffer.
 * @param len      Bytes to read.
 * @return true if @p dst was filled.
 */
bool settingsStorageRead(void *dst, uint32_t len);

/**
 * @brief Overwrite the persisted block.
 * @param src Source buffer.
 * @param len Bytes to write.
 * @return true if the write completed.
 */
bool settingsStorageWrite(const void *src, uint32_t len);

/** @} */

/**
 * @brief CRC-32, the common reflected polynomial (0xEDB88320).
 *
 * Exposed so the tests can corrupt a block deliberately and confirm the load
 * path rejects it, rather than asserting that some opaque number is unchanged.
 *
 * @param data Bytes to sum.
 * @param len  Number of bytes.
 * @return The checksum.
 */
uint32_t settingsCrc32(const void *data, uint32_t len);
