/**
 * @file st7789.cpp
 * @brief ST7789T3 init sequence, SPI/DMA plumbing and the dirty-band flush.
 */

#include "st7789.h"
#include "board_pins.h"
#include "config.h"
#include "gfx.h"

#include <Arduino.h>
#include <hardware/spi.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>

#define LCD_SPI LCD_SPI_PORT

static int s_dma = -1; /**< DMA channel claimed for pixel transfers. */

/** @brief Block until the SPI peripheral has drained. */
static inline void spiWait() {
	while (spi_is_busy(LCD_SPI)) tight_loop_contents();
}

/** @brief Switch to 8-bit frames, used for commands and parameters. */
static inline void spiFormat8() {
	spiWait();
	spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

/**
 * @brief Switch to 16-bit frames, used for pixel data.
 *
 * SPI transmits MSB first, so a 16-bit frame puts an RGB565 word on the wire
 * big-endian — exactly what the panel wants, with no software byte swapping.
 */
static inline void spiFormat16() {
	spiWait();
	spi_set_format(LCD_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

/** @brief Send a single command byte with DC low. */
static void cmd(uint8_t c) {
	spiFormat8();
	gpio_put(PIN_LCD_DC, 0);
	gpio_put(PIN_LCD_CS, 0);
	spi_write_blocking(LCD_SPI, &c, 1);
	spiWait();
	gpio_put(PIN_LCD_CS, 1);
}

/** @brief Send @p n parameter bytes with DC high. */
static void data(const uint8_t *d, size_t n) {
	if (!n) return;
	spiFormat8();
	gpio_put(PIN_LCD_DC, 1);
	gpio_put(PIN_LCD_CS, 0);
	spi_write_blocking(LCD_SPI, d, n);
	spiWait();
	gpio_put(PIN_LCD_CS, 1);
}

/** @brief Send a command followed by its parameters. */
static void cmdData(uint8_t c, const uint8_t *d, size_t n) {
	cmd(c);
	data(d, n);
}

/**
 * @brief MADCTL value for a given rotation.
 *
 * MADCTL bits: 7 = MY row order, 6 = MX column order, 5 = MV row/column
 * exchange, 3 = RGB/BGR order. This panel is wired RGB, so bit 3 stays clear.
 *
 * @param rotation See @ref LCD_ROTATION. Only the low two bits are used.
 * @return Byte to write to register 0x36.
 */
static uint8_t madctlFor(int rotation) {
	switch (rotation & 3) {
		case 1:  return 0x60;  // landscape
		case 2:  return 0xC0;  // portrait flipped
		case 3:  return 0xA0;  // landscape flipped
		default: return 0x00;  // portrait, USB at the bottom
	}
}

void st7789Init() {
	// --- GPIO ---
	gpio_init(PIN_LCD_DC);  gpio_set_dir(PIN_LCD_DC, GPIO_OUT);  gpio_put(PIN_LCD_DC, 1);
	gpio_init(PIN_LCD_CS);  gpio_set_dir(PIN_LCD_CS, GPIO_OUT);  gpio_put(PIN_LCD_CS, 1);
	gpio_init(PIN_LCD_RST); gpio_set_dir(PIN_LCD_RST, GPIO_OUT); gpio_put(PIN_LCD_RST, 1);

	// --- SPI0 ---
	spi_init(LCD_SPI, LCD_SPI_HZ);
	spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
	gpio_set_function(PIN_LCD_SCK,  GPIO_FUNC_SPI);
	gpio_set_function(PIN_LCD_MOSI, GPIO_FUNC_SPI);

	// --- DMA ---
	if (s_dma < 0) s_dma = dma_claim_unused_channel(true);

	// --- hardware reset (note: this also resets the CST816D touch chip,
	//     the two RESET lines are the same net on this board) ---
	gpio_put(PIN_LCD_RST, 1); delay(20);
	gpio_put(PIN_LCD_RST, 0); delay(20);
	gpio_put(PIN_LCD_RST, 1); delay(120);

	cmd(0x01);            // SWRESET
	delay(150);
	cmd(0x11);            // SLPOUT
	delay(120);

	uint8_t b;
	b = 0x55;                 cmdData(0x3A, &b, 1);   // COLMOD: 16bpp
	b = madctlFor(LCD_ROTATION); cmdData(0x36, &b, 1); // MADCTL

	static const uint8_t porctrl[]  = {0x0C, 0x0C, 0x00, 0x33, 0x33};
	cmdData(0xB2, porctrl, sizeof(porctrl));           // PORCTRL
	b = 0x35; cmdData(0xB7, &b, 1);                    // GCTRL
	b = 0x19; cmdData(0xBB, &b, 1);                    // VCOMS
	b = 0x2C; cmdData(0xC0, &b, 1);                    // LCMCTRL
	b = 0x01; cmdData(0xC2, &b, 1);                    // VDVVRHEN
	b = 0x12; cmdData(0xC3, &b, 1);                    // VRHS
	b = 0x20; cmdData(0xC4, &b, 1);                    // VDVS
	b = 0x0F; cmdData(0xC6, &b, 1);                    // FRCTRL2: ~60 Hz

	static const uint8_t pwctrl1[] = {0xA4, 0xA1};
	cmdData(0xD0, pwctrl1, sizeof(pwctrl1));           // PWCTRL1

	static const uint8_t pvgam[] = {0xD0,0x04,0x0D,0x11,0x13,0x2B,0x3F,
	                                0x54,0x4C,0x18,0x0D,0x0B,0x1F,0x23};
	cmdData(0xE0, pvgam, sizeof(pvgam));               // PVGAMCTRL
	static const uint8_t nvgam[] = {0xD0,0x04,0x0C,0x11,0x13,0x2C,0x3F,
	                                0x44,0x51,0x2F,0x1F,0x1F,0x20,0x23};
	cmdData(0xE1, nvgam, sizeof(nvgam));               // NVGAMCTRL

	cmd(0x21);            // INVON  (IPS panels need inversion)
	cmd(0x13);            // NORON
	delay(10);
	cmd(0x29);            // DISPON
	delay(50);

	// --- backlight on GPIO15 via an NPN, PWM-able ---
	pinMode(PIN_LCD_BL, OUTPUT);
	analogWriteFreq(20000);
	analogWriteRange(255);
	st7789SetBacklight(LCD_BACKLIGHT_DEFAULT);
}

void st7789SetBacklight(uint8_t level) {
	analogWrite(PIN_LCD_BL, level);
}

void st7789Sleep(bool on) {
	cmd(on ? 0x10 : 0x11);
	delay(120);
}

/** @brief Set the panel's write window via CASET/RASET, inclusive bounds. */
static void setWindow(int x0, int y0, int x1, int y1) {
	uint8_t buf[4];
	buf[0] = (uint8_t)(x0 >> 8); buf[1] = (uint8_t)x0;
	buf[2] = (uint8_t)(x1 >> 8); buf[3] = (uint8_t)x1;
	cmdData(0x2A, buf, 4);                             // CASET
	buf[0] = (uint8_t)(y0 >> 8); buf[1] = (uint8_t)y0;
	buf[2] = (uint8_t)(y1 >> 8); buf[3] = (uint8_t)y1;
	cmdData(0x2B, buf, 4);                             // RASET
}

void st7789Blit(int x0, int y0, int x1, int y1, const uint16_t *pixels) {
	uint32_t n = (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1);
	if (!n) return;

	setWindow(x0, y0, x1, y1);
	cmd(0x2C);                                         // RAMWR

	spiFormat16();
	gpio_put(PIN_LCD_DC, 1);
	gpio_put(PIN_LCD_CS, 0);

	dma_channel_config c = dma_channel_get_default_config(s_dma);
	channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
	channel_config_set_dreq(&c, spi_get_dreq(LCD_SPI, true));
	channel_config_set_read_increment(&c, true);
	channel_config_set_write_increment(&c, false);
	dma_channel_configure(s_dma, &c,
	                      (volatile void *)&spi_get_hw(LCD_SPI)->dr,
	                      pixels, n, true);
	dma_channel_wait_for_finish_blocking(s_dma);

	spiWait();
	gpio_put(PIN_LCD_CS, 1);
	spiFormat8();
}

void st7789FlushDirty() {
	int n = gfxDirtyCount();
	const uint16_t *fb = gfxBuffer();
	for (int i = 0; i < n; i++) {
		int y0, y1;
		gfxDirtyBand(i, &y0, &y1);
		st7789Blit(0, y0, GFX_W - 1, y1, fb + (size_t)y0 * GFX_W);
	}
	gfxClearDirty();
}
