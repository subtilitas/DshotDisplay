/**
 * @file board_pins.h
 * @brief Pin map for the Waveshare RP2350-Touch-LCD-2.
 *
 * Derived from the official schematic rather than the wiki, whose pinout is
 * only published as an image:
 * https://files.waveshare.com/wiki/RP2350-Touch-LCD-2/RP2350-Touch-LCD-2.pdf
 *
 * Full GPIO allocation on this board:
 *
 * | GPIO   | Function      | Notes                                   |
 * |--------|---------------|-----------------------------------------|
 * | 0..7   | CAM_D0..D7    | free if no camera fitted                 |
 * | 8      | CAM_VSYNC     | free if no camera fitted                 |
 * | 9      | CAM_HREF      | free if no camera fitted                 |
 * | 10     | CAM_PCLK      | free if no camera fitted                 |
 * | 11     | CAM_XCLK      | free if no camera fitted                 |
 * | 12     | I2C0 SDA      | shared by CST816D touch **and** QMI8658  |
 * | 13     | I2C0 SCL      | shared by CST816D touch **and** QMI8658  |
 * | 14     | IMU_INT1      |                                          |
 * | 15     | LCD_BL        | NPN base via 1 k to backlight LEDA       |
 * | 16     | LCD_DC        |                                          |
 * | 17     | LCD_CS        | via R16; R14 0R may also tie CS to GND   |
 * | 18     | LCD_CLK       | SPI0 SCK                                 |
 * | 19     | LCD_DIN       | SPI0 TX / MOSI                           |
 * | 20     | LCD_RST       | also drives TP_RESET, 10 k pull-up       |
 * | 21     | CAM_PWDN      | free if no camera fitted                 |
 * | 22     | CAM_SCCB_SDA  | free if no camera fitted                 |
 * | 23     | CAM_SCCB_SCL  | free if no camera fitted                 |
 * | 24     | SD_MISO       | SPI1 RX                                  |
 * | 25     | SD_CS         |                                          |
 * | 26     | SD_SCLK       | SPI1 SCK                                 |
 * | 27     | SD_MOSI       | SPI1 TX                                  |
 * | 28     | BAT_ADC       | ADC2, 200k/100k divider                  |
 * | 29     | TP_INT        | CST816D interrupt, active low            |
 *
 * Breakout headers (14 pins each), for hooking up the ESC signal wire. Entries
 * marked `*` are already used by an on-board peripheral and must not be
 * repurposed:
 *
 *     P1: 1=GP7  2=GP9  3=GP8  4=GP22 5=GP21 6=GP18* 7=GP23
 *         8=GP11 9=GP6  10=GP5 11=GP20* 12=GP19* 13=GND 14=5V
 *     P2: 1=3V3  2=GND  3=GP28* 4=GP29* 5=GP13* 6=GP12*
 *         7=GP2  8=GP1  9=GP3  10=GP0 11=GP4  12=GP10 13=GND 14=VBAT
 *
 * @note GPIO20 is the same net as the touch controller's reset line, so
 *       resetting the LCD also resets the CST816D. The touch driver must be
 *       initialised *after* st7789Init().
 */

#pragma once

/**
 * @defgroup pins_lcd LCD — ST7789T3, 240x320, SPI0
 * @{
 */
#define PIN_LCD_BL    15   /**< Backlight enable / PWM (NPN low-side switch). */
#define PIN_LCD_DC    16   /**< Data / command select. */
#define PIN_LCD_CS    17   /**< Chip select, active low. */
#define PIN_LCD_SCK   18   /**< SPI0 SCK. */
#define PIN_LCD_MOSI  19   /**< SPI0 TX. The panel is write-only here. */
#define PIN_LCD_RST   20   /**< Hardware reset. Shared with the touch chip. */
#define LCD_SPI_PORT  spi0 /**< SPI instance driving the panel. */
/** @} */

/**
 * @defgroup pins_touch Capacitive touch — CST816D on I2C0
 * @brief Shares the I2C bus with the IMU.
 * @{
 */
#define PIN_I2C_SDA   12          /**< I2C0 SDA, shared with the IMU. */
#define PIN_I2C_SCL   13          /**< I2C0 SCL, shared with the IMU. */
#define PIN_TP_INT    29          /**< Touch interrupt, active low. */
#define PIN_TP_RST    PIN_LCD_RST /**< Physically the same net as LCD reset. */
#define CST816_I2C_ADDR 0x15      /**< 7-bit I2C address of the CST816D. */
/** @} */

/**
 * @defgroup pins_imu IMU — QMI8658C
 * @brief Present on the board but unused by this firmware.
 * @{
 */
#define PIN_IMU_INT1  14     /**< IMU interrupt 1. */
#define QMI8658_I2C_ADDR 0x6B /**< 7-bit I2C address of the QMI8658C. */
/** @} */

/**
 * @defgroup pins_bat Battery sense
 * @{
 */
#define PIN_BAT_ADC   28   /**< Divider midpoint, GPIO28 / ADC2. */
#define BAT_ADC_CHAN  A2   /**< Arduino analog channel for PIN_BAT_ADC. */
#define BAT_DIVIDER   3.0f /**< 200 k / 100 k divider: VBAT = Vadc * 3. */
/** @} */

/**
 * @defgroup pins_sd microSD — SPI1
 * @brief Unused by this firmware; listed so the pins are not reused.
 * @{
 */
#define PIN_SD_MISO   24 /**< SPI1 RX. */
#define PIN_SD_CS     25 /**< Card select. */
#define PIN_SD_SCK    26 /**< SPI1 SCK. */
#define PIN_SD_MOSI   27 /**< SPI1 TX. */
/** @} */
