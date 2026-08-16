/**
 * @file am32_eeprom.h
 * @brief AM32 ESC settings block: layout, decoding and presentation.
 *
 * The byte layout matches the AM32 configurator's `EepromLayout`. Offsets are
 * absolute within the settings region, which starts at @ref AM32_EEPROM_ADDR in
 * the ESC's flash.
 *
 * Region map:
 *
 * | Range      | Size | Contents                          |
 * |------------|------|-----------------------------------|
 * | 0x00..0x04 | 5    | boot byte, layout and FW revisions |
 * | 0x05..0x10 | 12   | device name (layout revision < 3)  |
 * | 0x05..0x0C | 8    | extra settings (layout revision 3+)|
 * | 0x11..0x2F | 31   | the settings proper                |
 * | 0x30..0xAF | 128  | startup melody                     |
 * | 0xB0..0xBF | 16   | CAN settings                       |
 *
 * Bytes 0x05..0x0C are overloaded: in layout revision 3 and later they hold
 * ramp/current-PID settings, and before that they are part of the device name
 * string. Fields carry a version gate so the wrong set is never shown.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Settings-page address on an STM32F051 part.
 *
 * @warning Not a universal constant. G071 parts keep theirs at 0x7E00 and F3
 *          parts at 0xF800, so use am32BlEepromAddr(), which reports whatever
 *          the connected ESC identified itself as. This is kept only as the
 *          documented F051 value.
 */
#define AM32_EEPROM_ADDR   0x7C00

/** @brief Bytes we read back: settings + melody + CAN block. */
#define AM32_EEPROM_SIZE   0xC0

/** @brief Bytes covering just the settings, excluding melody and CAN. */
#define AM32_SETTINGS_SIZE 0x30

/** @brief Offsets of the fixed header fields. */
enum {
	AM32_OFF_BOOT_BYTE       = 0x00, /**< Non-zero once the ESC has valid settings. */
	AM32_OFF_LAYOUT_REVISION = 0x01, /**< Governs which optional fields exist. */
	AM32_OFF_BOOTLOADER_REV  = 0x02,
	AM32_OFF_MAIN_REVISION   = 0x03,
	AM32_OFF_SUB_REVISION    = 0x04,
	AM32_OFF_NAME            = 0x05, /**< 12 bytes, layout revision < 3 only. */
	AM32_OFF_ADVANCE         = 0x17, /**< Timing advance; two encodings. */
	AM32_NAME_LEN            = 12,
	AM32_OFF_MELODY          = 0x30,
	AM32_MELODY_LEN          = 128,
	AM32_OFF_CAN             = 0xB0,
	AM32_CAN_LEN             = 16,
};

/** @brief How a raw byte is turned into something a human can read. */
enum Am32Type : uint8_t {
	A32_BOOL,   /**< Off / On. */
	A32_ENUM,   /**< Index into Am32Field::names. */
	A32_RAW,    /**< Plain integer, shown with Am32Field::unit. */
	A32_SCALED, /**< `(raw * mul) / div + add`, shown with one decimal if div > 1. */
	A32_ADVANCE,/**< Timing advance. Two encodings by value. @see am32AdvanceDeciDeg() */
};

/**
 * @brief Timing advance in tenths of a degree, from the raw EEPROM byte.
 *
 * This field has two encodings and the byte itself is what says which, exactly
 * as AM32's own start-up code decides it:
 *
 * - **0..3** — the pre-1.90 format. Four steps of 7.5 degrees, 0 to 22.5.
 * - **10..42** — 1.90 and later. `raw - 10` steps of 0.9375 degrees (60/64,
 *   which is the resolution the commutation timer actually works in), 0 to 30.
 * - anything else is not a value AM32 wrote. It substitutes its own default;
 *   this returns -1 so the screen can say so rather than inventing an angle.
 *
 * Treating the field as the old encoding alone is what made a modern ESC read
 * back nonsense until the first `-` or `+` press, which clamped the byte into
 * 0..4 and made the number plausible again. A display that becomes correct
 * *because you edited it* is worse than one that is obviously wrong, because
 * the edit is what you then write back.
 *
 * @param raw Byte at offset 0x17, `AM32_OFF_ADVANCE`.
 * @return Tenths of a degree, or -1 if the byte is in neither encoding.
 */
static inline int16_t am32AdvanceDeciDeg(uint8_t raw) {
	if (raw <= 3)                 return (int16_t)(raw * 75);
	if (raw >= 10 && raw <= 42)   return (int16_t)((raw - 10) * 75 / 8);
	return -1;
}

/**
 * @brief The raw byte for a given advance step, in the modern encoding.
 *
 * Editing always writes 1.90-format bytes, whatever was read. AM32 accepts them
 * on any version — its start-up code converts in that direction anyway — and
 * writing the format the ESC's own configurator writes is the one choice that
 * cannot surprise somebody who opens it there afterwards.
 *
 * @param step 0..32, in 0.9375 degree increments.
 * @return Byte to store.
 */
static inline uint8_t am32AdvanceRaw(uint8_t step) {
	if (step > 32) step = 32;
	return (uint8_t)(step + 10);
}

/**
 * @brief The 0..32 advance step a raw byte represents, in either encoding.
 *
 * Derived from the byte directly rather than by converting the angle back,
 * because tenths of a degree do not divide evenly by 0.9375: a round trip
 * through am32AdvanceDeciDeg() loses a step, and an editor built on it stepped
 * up by one and back down by two.
 *
 * The old-format conversion is `raw << 3`, which is exactly what AM32's own
 * start-up code does with it.
 *
 * @param raw Byte at offset 0x17, `AM32_OFF_ADVANCE`.
 * @return Step 0..32. A byte in neither encoding gives 16, AM32's own
 *         substitute, so editing from it starts where the ESC actually is.
 */
static inline uint8_t am32AdvanceStep(uint8_t raw) {
	if (raw <= 3)               return (uint8_t)(raw << 3);
	if (raw >= 10 && raw <= 42) return (uint8_t)(raw - 10);
	return 16;
}

/**
 * @brief One editable setting.
 *
 * @p minVer and @p maxVer gate the field against the EEPROM's layout revision
 * at offset 0x01; 0 means unbounded on that side.
 */
struct Am32Field {
	const char *group;      /**< Section heading this field sits under. */
	const char *name;       /**< Display label. */
	uint8_t     offset;     /**< Byte offset within the settings region. */
	Am32Type    type;       /**< Presentation. */
	uint8_t     rawMin;     /**< Lowest value the editor will produce. */
	uint8_t     rawMax;     /**< Highest value the editor will produce. */
	uint8_t     step;       /**< Editor increment. */
	uint8_t     minVer;     /**< Minimum layout revision, 0 = any. */
	uint8_t     maxVer;     /**< Maximum layout revision, 0 = any. */
	int16_t     mul;        /**< A32_SCALED numerator. */
	int16_t     div;        /**< A32_SCALED denominator. */
	int16_t     add;        /**< A32_SCALED offset, applied after scaling. */
	const char *unit;       /**< Suffix, or "" for none. */
	const char *const *names; /**< A32_ENUM labels. */
	uint8_t     nameCount;  /**< Number of A32_ENUM labels. */
};

/** @brief The full field table. */
extern const Am32Field AM32_FIELDS[];

/** @brief Number of entries in @ref AM32_FIELDS. */
extern const uint16_t AM32_FIELD_COUNT;

/**
 * @brief Whether a field applies to a given EEPROM layout revision.
 * @param f          Field to test.
 * @param layoutRev  Value read from offset 0x01.
 */
bool am32FieldApplies(const Am32Field *f, uint8_t layoutRev);

/**
 * @brief Render a field's current value into @p out.
 *
 * Never writes more than @p outLen bytes and always NUL-terminates.
 *
 * @param f      Field to format.
 * @param eeprom The settings region as read from the ESC.
 * @param out    Destination buffer.
 * @param outLen Size of @p out.
 */
void am32FormatValue(const Am32Field *f, const uint8_t *eeprom, char *out, int outLen);

/**
 * @brief Copy the device name out, NUL-terminated.
 *
 * Layout revision 3 and later reuse those bytes for settings, so this yields an
 * empty string there rather than nonsense.
 *
 * @param eeprom The settings region.
 * @param out    Destination, at least AM32_NAME_LEN + 1 bytes.
 */
void am32DeviceName(const uint8_t *eeprom, char *out);

/**
 * @brief Adjust a field by @p dir steps, clamped to its range.
 * @param f      Field to change.
 * @param eeprom Settings region, modified in place.
 * @param dir    +1 or -1.
 */
void am32Adjust(const Am32Field *f, uint8_t *eeprom, int dir);

/** @brief True if the block looks like initialised AM32 settings. */
bool am32Plausible(const uint8_t *eeprom);
