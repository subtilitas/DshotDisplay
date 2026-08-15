/**
 * @file board_desc_lcd2.cpp
 * @brief The 2.0" board, as a value.
 *
 * Includes exactly one board header, which is the point: this file and its
 * sibling define the same macro names with different values, so they can never
 * share a translation unit. @see board_desc.h
 */

#include "board_desc.h"
#include "config.h"
#include "board_rp2350_touch_lcd_2.h"
#include "touch.h"

#include <hardware/spi.h>
#include <hardware/i2c.h>

extern const TouchDriver TOUCH_DRIVER_CST816D;

const BoardDesc BOARD_DESC_LCD_2 = {
	/* label      */ BOARD_LABEL,

	/* lcdSpi     */ (void *)LCD_SPI_PORT,
	/* lcdBl      */ PIN_LCD_BL,
	/* lcdDc      */ PIN_LCD_DC,
	/* lcdCs      */ PIN_LCD_CS,
	/* lcdSck     */ PIN_LCD_SCK,
	/* lcdMosi    */ PIN_LCD_MOSI,
	/* lcdRst     */ PIN_LCD_RST,

	/* i2c        */ (void *)BOARD_I2C,
	/* sda        */ PIN_I2C_SDA,
	/* scl        */ PIN_I2C_SCL,
	/* tpInt      */ PIN_TP_INT,
	/* tpRst      */ PIN_TP_RST,
	/* tpRstShared*/ TOUCH_RST_SHARED_WITH_LCD != 0,
	/* tpAddr     */ CST816_I2C_ADDR,
	/* touch      */ &TOUCH_DRIVER_CST816D,

	/* batAdcPin  */ PIN_BAT_ADC,
	/* batAdcChan */ BAT_ADC_CHAN,
	/* batDivider */ BAT_DIVIDER,
	/* batEnPin   */ -1,          /* no power latch on this board */

	/* sdIface    */ SD_IFACE_DESC_SPI,
	/* sdSck      */ PIN_SD_SCK,
	/* sdCmd      */ PIN_SD_MOSI, /* CMD is MOSI in SPI mode */
	/* sdD0       */ PIN_SD_MISO, /* D0 is MISO in SPI mode  */
	/* sdD1       */ 0,           /* unused in SPI mode      */
	/* sdD2       */ 0,
	/* sdD3       */ PIN_SD_CS,   /* D3 is CS in SPI mode    */

	/* freeGpio   */ BOARD_FREE_GPIO_MASK,
	/* dshotPin   */ DSHOT_PIN_LCD_2,
	/* kissPin    */ KISS_PIN_LCD_2,
	/* kissEnable */ KISS_ENABLE_LCD_2 != 0,
};

/*
 * The pin defaults are macros, so they are overridable from the build -- and an
 * override that cannot work must fail here rather than be repaired in silence
 * on the bench. settingsValidate() would quietly move an occupied ESC pin back
 * to the board default and switch a colliding KISS wire off, which is right for
 * a stored block nobody chose and wrong for a value someone typed on a command
 * line. @see cfg_pin_defaults
 *
 * The `& 31` keeps the shift in range when the pin is nonsense, so the message
 * that fires is this one rather than the compiler's own about the shift.
 */
static_assert(DSHOT_PIN_LCD_2 <= 29 &&
              ((BOARD_FREE_GPIO_MASK >> (DSHOT_PIN_LCD_2 & 31)) & 1u),
              "DSHOT_PIN_LCD_2 is not a free GPIO on the 2.0-inch board");
static_assert(KISS_PIN_LCD_2 <= 29 &&
              ((BOARD_FREE_GPIO_MASK >> (KISS_PIN_LCD_2 & 31)) & 1u),
              "KISS_PIN_LCD_2 is not a free GPIO on the 2.0-inch board");
static_assert(DSHOT_PIN_LCD_2 != KISS_PIN_LCD_2,
              "the ESC and telemetry wires cannot share a pin");
