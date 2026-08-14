/**
 * @file board_desc_lcd2.cpp
 * @brief The 2.0" board, as a value.
 *
 * Includes exactly one board header, which is the point: this file and its
 * sibling define the same macro names with different values, so they can never
 * share a translation unit. @see board_desc.h
 */

#include "board_desc.h"
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
	/* dshotPin   */ 4,
	/* kissPin    */ 5,
	/* kissEnable */ true,
};
