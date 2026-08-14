/**
 * @file board_desc.h
 * @brief The board as data, so one image can drive either one.
 *
 * @ref board_pins.h resolves the board with the preprocessor, which is correct
 * and cheap and produces one firmware per board. That is two images to build,
 * two to publish, and two to confuse — and the 2.8" image asserts a power latch
 * the 2.0" does not, so flashing the wrong one is not a harmless mistake.
 *
 * This is the same information carried as a value instead. A build can hold
 * both descriptors and pick between them at run time.
 *
 * @section board_desc_tu One board per translation unit
 *
 * The two board headers define about eighteen of the same macro names with
 * different values — `PIN_LCD_SCK` is 18 on one and 10 on the other, and so on
 * down the list. Including both in one file is a redefinition error on every
 * one of them, and no amount of namespacing helps, because macros do not have
 * scope.
 *
 * So each descriptor is built in its own `.cpp`, which includes exactly one
 * board header. The two never meet. That is the whole trick, and it is why this
 * refactor is mechanical rather than a rewrite: `board_desc_lcd2.cpp` and
 * `board_desc_lcd2_8.cpp` are each about forty lines of struct initialiser
 * naming the macros their own header defines.
 *
 * @section board_desc_selection Which board
 *
 * Detected on every boot, and never remembered. A unified image *identifies*
 * the board — see board_probe.h, whose probe is deliberately narrower than the
 * pin-map guessing `board.h` warns about: it only ever drives two pins as
 * open-drain I2C, on lines shown benign on both schematics, and treats
 * anything but exactly one answer as "unknown", which keeps the power latch
 * held and every output silent.
 *
 * There is no picker and no stored override, because the two ways of being
 * wrong are not symmetrical. A probe that misreads the hardware fails at boot,
 * before a pin is driven, and says so. A stored choice that misreads it is
 * applied by the boot that reads it — building the display, the touch
 * controller and the pin map for hardware that is not there — and the screen
 * that could have corrected it is the screen that no longer comes up. The
 * settings block still carries a board id, but only as a record of where it
 * was written. @see settingsValidate()
 */

#pragma once

#include <stdint.h>

/*
 * Included from sd_hw_config.c, which is C because the FatFs library declares
 * its hooks with C linkage. So this header stays C-compatible: a plain enum
 * rather than an enum class, and `extern "C"` around the functions.
 */
#ifdef __cplusplus
struct TouchState;
extern "C" {
#else
#include <stdbool.h>
struct TouchState;
#endif

/** @brief Which SD interface a board wires its card slot for. */
enum SdIfaceDesc {
	SD_IFACE_DESC_SPI = 0,  /**< Four lines on hardware SPI, one bit wide. */
	SD_IFACE_DESC_SDIO = 1, /**< Six lines on the PIO SDIO program, four bits. */
};

/**
 * @brief One touch controller driver.
 *
 * A pair of function pointers rather than the two free functions the drivers
 * used to export. Both exported exactly `touchInit` and `touchPoll`, which is
 * fine while only one of them ever compiles and a duplicate-symbol error the
 * moment both do.
 */
struct TouchDriver {
	const char *name;                       /**< Chip name, for diagnostics. */
	bool (*init)(void);                     /**< Bring the controller up. */
	void (*poll)(struct TouchState *t);     /**< Sample it. */
};

/**
 * @brief Everything this firmware needs to know about a board.
 *
 * Deliberately not a superset of the board headers. Peripherals the firmware
 * does not drive — the IMU, the RTC, the audio codec — stay documented in the
 * headers as pins not to reuse, and do not appear here: a descriptor field
 * nothing reads is a field that goes stale without anyone noticing.
 */
struct BoardDesc {
	/** @brief Short name, shown on the splash and in the board picker. */
	const char *label;

	/**
	 * @name Panel
	 * @{
	 */
	void    *lcdSpi;    /**< `spi_inst_t *`, opaque here to keep the SDK out. */
	uint8_t  lcdBl;     /**< Backlight, PWM. */
	uint8_t  lcdDc;     /**< Data / command select. */
	uint8_t  lcdCs;     /**< Chip select. */
	uint8_t  lcdSck;    /**< Clock. */
	uint8_t  lcdMosi;   /**< Data out. */
	uint8_t  lcdRst;    /**< Reset. */
	/** @} */

	/**
	 * @name Touch
	 * @{
	 */
	void    *i2c;       /**< `i2c_inst_t *`. */
	uint8_t  sda;       /**< I2C data. */
	uint8_t  scl;       /**< I2C clock. */
	uint8_t  tpInt;     /**< Touch interrupt, active low. */
	uint8_t  tpRst;     /**< Touch reset. May be the same net as @ref lcdRst. */
	bool     tpRstSharedWithLcd; /**< True when st7789Init() already pulsed it. */
	uint8_t  tpAddr;    /**< 7-bit I2C address. */
	const struct TouchDriver *touch; /**< Which driver talks to it. */
	/** @} */

	/**
	 * @name Battery
	 * @{
	 */
	uint8_t  batAdcPin;  /**< Divider midpoint. */
	uint8_t  batAdcChan; /**< ADC input index, which is not the GPIO number. */
	float    batDivider; /**< VBAT = Vadc * this. */
	/**
	 * @brief Power latch, or -1 where the board has none.
	 *
	 * The 2.8" dies mid-boot on battery without this driven high. @see main.cpp
	 * for why a unified image asserts it before it knows which board it is on.
	 */
	int8_t   batEnPin;
	/** @} */

	/**
	 * @name microSD
	 * @{
	 */
	enum SdIfaceDesc sdIface; /**< Which interface the slot is wired for. */
	uint8_t  sdSck;     /**< Clock. */
	uint8_t  sdCmd;     /**< CMD, or MOSI in SPI mode. */
	uint8_t  sdD0;      /**< D0, or MISO in SPI mode. */
	uint8_t  sdD1;      /**< D1. SDIO only. */
	uint8_t  sdD2;      /**< D2. SDIO only. */
	uint8_t  sdD3;      /**< D3, or chip select in SPI mode. */
	/** @} */

	/**
	 * @brief GPIOs free for the ESC or telemetry wire, as a bitmask.
	 *
	 * The thing the SETUP screen steps through. Everything outside it drives an
	 * on-board peripheral. @see settingsPinFree()
	 */
	uint32_t freeGpioMask;

	/** @brief ESC pin a board with blank flash starts on. */
	uint8_t  defaultDshotPin;
	/** @brief KISS pin a board with blank flash starts on. */
	uint8_t  defaultKissPin;
	/** @brief Whether a board with blank flash expects a KISS wire. */
	bool     defaultKissEnable;
};

/**
 * @defgroup board_desc_ids Board identifiers
 * @brief Stored in the settings block, so these values are on-flash format.
 * @{
 */
#define BOARD_ID_UNSET     0  /**< Nothing identified. Never stored. */
#define BOARD_ID_LCD_2     2  /**< Waveshare RP2350-Touch-LCD-2. */
#define BOARD_ID_LCD_2_8  28  /**< Waveshare RP2350-Touch-LCD-2.8. */
/** @} */

/** @brief Descriptor for the 2.0" board. Built by board_desc_lcd2.cpp. */
extern const struct BoardDesc BOARD_DESC_LCD_2;

/** @brief Descriptor for the 2.8" board. Built by board_desc_lcd2_8.cpp. */
extern const struct BoardDesc BOARD_DESC_LCD_2_8;

/**
 * @brief The board this firmware is currently driving.
 *
 * Never null after boardSelect() has run, and boardSelect() runs before
 * anything touches a pin.
 */
extern const struct BoardDesc *g_board;

/**
 * @brief Point @ref g_board at the descriptor for @p boardId.
 *
 * @param boardId One of @ref board_desc_ids.
 * @return true if @p boardId named a board this image knows about.
 */
bool boardSelect(uint8_t boardId);

/**
 * @brief Which board @ref g_board currently describes.
 * @return One of @ref board_desc_ids, or @ref BOARD_ID_UNSET.
 */
uint8_t boardId();

/**
 * @brief How many boards this image can drive.
 *
 * One for a single-board build, two for a unified one. What it gates is the
 * boot probe: an image that can drive only one board has nothing to identify,
 * and asks nothing of the I2C buses.
 *
 * @return Number of boards this image holds descriptors for.
 */
int boardCount();

/**
 * @brief The @p i th board this image holds.
 * @param i Index below boardCount().
 * @return Its descriptor, or nullptr if @p i is out of range.
 */
const struct BoardDesc *boardAt(int i);

/**
 * @brief The board id of the @p i th board this image holds.
 * @param i Index below boardCount().
 * @return One of @ref board_desc_ids, or @ref BOARD_ID_UNSET.
 */
uint8_t boardIdAt(int i);

/**
 * @brief Copy the active board's SD wiring into the FatFs driver's structs.
 *
 * Must run before the card is mounted. Lives in sd_hw_config.c, which is the
 * only file the library links against by name.
 */
void sdHwConfigApply(void);

#ifdef __cplusplus
}
#endif
