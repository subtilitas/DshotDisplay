/**
 * @file board_desc_lcd2_8.cpp
 * @brief The 2.8" board, as a value. @see board_desc_lcd2.cpp
 */

#include "board_desc.h"
#include "board_rp2350_touch_lcd_2_8.h"
#include "touch.h"

#include <hardware/spi.h>
#include <hardware/i2c.h>

extern const TouchDriver TOUCH_DRIVER_CST328;

const BoardDesc BOARD_DESC_LCD_2_8 = {
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
	/* tpAddr     */ CST328_I2C_ADDR,
	/* touch      */ &TOUCH_DRIVER_CST328,

	/* batAdcPin  */ PIN_BAT_ADC,
	/* batAdcChan */ BAT_ADC_CHAN,
	/* batDivider */ BAT_DIVIDER,
	/* batEnPin   */ PIN_BAT_EN,

	/* sdIface    */ SD_IFACE_DESC_SDIO,
	/* sdSck      */ PIN_SD_SCK,
	/* sdCmd      */ PIN_SD_CMD,
	/* sdD0       */ PIN_SD_D0,
	/* sdD1       */ PIN_SD_D1,
	/* sdD2       */ PIN_SD_D2,
	/* sdD3       */ PIN_SD_D3,

	/* freeGpio   */ BOARD_FREE_GPIO_MASK,
	/* dshotPin   */ 29,
	/* kissPin    */ 29,
	/* kissEnable */ false,
};

/*
 * The two rules the SDIO PIO program encodes and cannot check itself. Asserted
 * here rather than in sd_hw_config.c because that file no longer sees this
 * board's pin macros in a unified build -- it takes them from the descriptor,
 * where they are no longer compile-time constants.
 */
static_assert(PIN_SD_D1 == PIN_SD_D0 + 1, "SDIO D1 must be D0 + 1");
static_assert(PIN_SD_D2 == PIN_SD_D0 + 2, "SDIO D2 must be D0 + 2");
static_assert(PIN_SD_D3 == PIN_SD_D0 + 3, "SDIO D3 must be D0 + 3");
static_assert(PIN_SD_SCK == (PIN_SD_D0 + 30) % 32,
              "SDIO CLK must be (D0 + 30) % 32; see rp2040_sdio.pio");
