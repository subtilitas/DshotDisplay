/**
 * @file board_rp2350_touch_lcd_2_8.h
 * @brief Pin map for the Waveshare RP2350-Touch-LCD-2.8 (2.8" panel).
 *
 * Included by @ref board_pins.h when @ref BOARD is
 * @ref BOARD_RP2350_TOUCH_LCD_2_8. Taken from the official schematic, which
 * unlike the wiki publishes a real netlist:
 * https://files.waveshare.com/wiki/RP2350-Touch-LCD-2.8/RP2350-Touch-LCD-2.8-Schematic.pdf
 *
 * Same RP2350A, same 240x320 ST7789T3 panel, same 200k/100k battery divider —
 * and almost nothing else in common with the 2.0" board. Every peripheral moved:
 *
 * | GPIO | Function     | Notes                                          |
 * |------|--------------|------------------------------------------------|
 * | 0    | UART0 TX     | J5 pin 2, J4 pin 9                              |
 * | 1    | UART0 RX     | J5 pin 1, J4 pin 10                             |
 * | 2    | I2S_BCK      | PCM5101A codec                                  |
 * | 3    | I2S_LRCK     | PCM5101A codec                                  |
 * | 4    | I2S_DIN      | PCM5101A codec                                  |
 * | 5    | RTC_INT      | PCF85063ATL                                     |
 * | 6    | I2C1 SDA     | CST328 **and** QMI8658 **and** PCF85063         |
 * | 7    | I2C1 SCL     | same three devices, plus J3 and J4              |
 * | 8    | IMU_INT1     |                                                 |
 * | 9    | IMU_INT2     |                                                 |
 * | 10   | LCD_SCK      | **SPI1** SCK                                    |
 * | 11   | LCD_MOSI     | SPI1 TX                                         |
 * | 12   | LCD_MISO     | SPI1 RX — wired, but this driver never reads    |
 * | 13   | LCD_CS       |                                                 |
 * | 14   | LCD_DC       |                                                 |
 * | 15   | LCD_RST      | **not** shared with the touch chip here          |
 * | 16   | LCD_BL       | NPN low-side switch via 1 k base resistor        |
 * | 17   | TP_RST       | CST328 reset, its own net                        |
 * | 18   | TP_INT       | CST328 interrupt, active low                     |
 * | 19   | SD_SCK       | microSD                                          |
 * | 20   | SD_CMD       | microSD (MOSI in SPI mode)                       |
 * | 21   | SD_D0        | microSD (MISO in SPI mode)                       |
 * | 22   | SD_D1        | microSD                                          |
 * | 23   | SD_D2        | microSD                                          |
 * | 24   | SD_D3        | microSD (CS in SPI mode)                         |
 * | 25   | KEY_BAT      | power button, 10 k pull-up, pressed = low        |
 * | 26   | BAT_EN       | battery power latch — see @ref PIN_BAT_EN        |
 * | 27   | BAT_ADC      | ADC1, 200k/100k divider                          |
 * | 28   | **free**     | J4 pin 11 — the ESC signal pin                   |
 * | 29   | **free**     | J4 pin 12                                        |
 *
 * There are no 2.54 mm headers on this board. Everything is brought out on
 * JST-SH 1.0 mm connectors, so an ESC lead needs a pigtail either way:
 *
 *     J4 (12P): 1=GND  2=VBUS 3=USB_N 4=USB_P 5=GND   6=3V3
 *               7=GP7* 8=GP6* 9=GP0   10=GP1  11=GP28 12=GP29
 *     J3  (4P): 1=GP7* 2=GP6* 3=3V3   4=GND
 *     J5  (4P): 1=GP1  2=GP0  3=3V3   4=GND
 *                                     (* = on the shared I2C bus, do not reuse)
 *
 * @note Unlike the 2.0" board, TP_RST is its own net. The touch driver has to
 *       pulse it itself; st7789Init() no longer does that for free.
 */

#pragma once

/**
 * @brief Human-readable board name, for the splash screen.
 *
 * Deliberately not `BOARD_NAME`: arduino-pico puts `-DBOARD_NAME="{build.board}"`
 * on every compile line, so that name is the core's and redefining it warns on
 * every translation unit.
 */
#define BOARD_LABEL "RP2350-TOUCH-LCD-2.8"

/**
 * @defgroup lcd28_pins_lcd LCD — ST7789T3, 240x320, SPI1
 * @{
 */
#define PIN_LCD_BL    16   /**< Backlight enable / PWM (NPN low-side switch). */
#define PIN_LCD_DC    14   /**< Data / command select. */
#define PIN_LCD_CS    13   /**< Chip select, active low. */
#define PIN_LCD_SCK   10   /**< SPI1 SCK. */
#define PIN_LCD_MOSI  11   /**< SPI1 TX. The panel is write-only here. */
#define PIN_LCD_MISO  12   /**< SPI1 RX. Wired on this board; left unconfigured. */
#define PIN_LCD_RST   15   /**< Hardware reset. LCD only — see `PIN_TP_RST`. */
#define LCD_SPI_PORT  spi1 /**< SPI instance driving the panel. */
/** @} */

/**
 * @defgroup lcd28_pins_touch Capacitive touch — CST328 on I2C1
 * @brief Shares the I2C bus with the IMU and the RTC.
 * @{
 */
#define BOARD_TOUCH_CST328 1          /**< Selects the cst328.cpp driver. */
#define BOARD_I2C     i2c1            /**< SDK I2C instance: GP6/GP7 are I2C1. */
#define PIN_I2C_SDA   6               /**< I2C1 SDA, shared with the IMU and RTC. */
#define PIN_I2C_SCL   7               /**< I2C1 SCL, shared with the IMU and RTC. */
#define PIN_TP_INT    18              /**< Touch interrupt, active low. */
#define PIN_TP_RST    17              /**< Touch reset, active low. Own net. */
#define TOUCH_RST_SHARED_WITH_LCD 0   /**< The touch driver owns TP_RST here. */
#define CST328_I2C_ADDR 0x1A          /**< 7-bit I2C address of the CST328. */
/** @} */

/**
 * @defgroup lcd28_pins_imu IMU — QMI8658C
 * @brief Present on the board but unused by this firmware.
 * @{
 */
#define PIN_IMU_INT1  8       /**< IMU interrupt 1. */
#define PIN_IMU_INT2  9       /**< IMU interrupt 2. */
#define QMI8658_I2C_ADDR 0x6B /**< 7-bit I2C address of the QMI8658C. */
/** @} */

/**
 * @defgroup lcd28_pins_rtc RTC — PCF85063ATL
 * @brief Present on the board but unused by this firmware.
 * @{
 */
#define PIN_RTC_INT   5        /**< RTC alarm / timer interrupt. */
#define PCF85063_I2C_ADDR 0x51 /**< 7-bit I2C address of the PCF85063ATL. */
/** @} */

/**
 * @defgroup lcd28_pins_bat Battery sense and power latch
 * @{
 */
#define PIN_BAT_ADC   27   /**< Divider midpoint, GPIO27 / ADC1. */
/**
 * @brief ADC input index for @ref PIN_BAT_ADC.
 *
 * The SDK numbers ADC inputs from GPIO26, so GPIO27 is input 1. This is not the
 * GPIO number, and passing 27 here would select a non-existent input and read
 * back garbage rather than failing.
 */
#define BAT_ADC_CHAN  1
#define BAT_DIVIDER   3.0f /**< 200 k / 100 k divider: VBAT = Vadc * 3. */

/**
 * @brief Battery power latch. Must be driven high to stay alive on battery.
 *
 * The power button only pulls the P-channel gate down while it is held. GPIO26
 * drives an NPN across the same node, so releasing the button cuts power unless
 * the firmware has already taken over the latch. Asserted first thing in
 * `setup()`, before anything slower has a chance to run.
 *
 * Irrelevant on USB, which feeds VSYS through its own diode.
 */
#define PIN_BAT_EN    26

/** @brief Power button, 10 k pull-up to 3V3; reads low while pressed. */
#define PIN_KEY_BAT   25
/** @} */

/**
 * @defgroup lcd28_pins_sd microSD
 * @brief Unused by this firmware; listed so the pins are not reused.
 * @{
 */
/*
 * This board is wired for **SDIO**, not SPI, and the pins satisfy every
 * constraint the PIO SDIO driver imposes -- which is not luck, it is how
 * Waveshare laid it out:
 *
 *     D1 = D0+1 = 22, D2 = D0+2 = 23, D3 = D0+3 = 24   (must be consecutive)
 *     CLK = (D0 + 30) % 32 = 19                        (fixed by the PIO program)
 *
 * Hardware SPI is impossible here in any case: SD_SCK is GP19, which is SPI0
 * **TX** in the RP2350 pin mux, not SCK. Four-bit SDIO is also faster than the
 * one-bit SPI mode the 2.0" board is stuck with.
 */
#define SD_IFACE_SDIO 1  /**< This board drives the card over PIO SDIO, 4-bit. */
#define PIN_SD_SCK    19 /**< Card clock. */
#define PIN_SD_CMD    20 /**< Command line; MOSI when driven as SPI. */
#define PIN_SD_D0     21 /**< Data 0; MISO when driven as SPI. */
#define PIN_SD_D1     22 /**< Data 1. */
#define PIN_SD_D2     23 /**< Data 2. */
#define PIN_SD_D3     24 /**< Data 3; card select when driven as SPI. */
/** @} */

/**
 * @defgroup lcd28_pins_audio Audio — PCM5101A codec into an APA2068 amplifier
 * @brief Unused by this firmware; listed so the pins are not reused.
 * @{
 */
#define PIN_I2S_BCK   2 /**< Bit clock. */
#define PIN_I2S_LRCK  3 /**< Word select. */
#define PIN_I2S_DIN   4 /**< Data in. */
/** @} */
