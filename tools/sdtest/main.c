/**
 * @file main.c
 * @brief Minimal standalone SD bring-up test. Nothing but the card.
 *
 * The point is to bisect a card that will not initialise: this firmware shares
 * only sd_hw_config.c with DshotDisplay, and starts no display, no touch, no
 * PIO, no second core. If the card mounts here but not in the firmware, the
 * fault is in the integration; if it fails here too, the integration is
 * exonerated and the problem is the board, the slot or the card.
 *
 * It also sweeps the SPI clock downwards. Some cards -- SanDisk in particular
 * are reputed to be fussy at init -- will not enumerate at a speed they will
 * happily run at afterwards, and that is invisible when only one speed is ever
 * tried.
 *
 * Output goes to USB serial at 115200.
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "ff.h"
#include "f_util.h"
#include "hw_config.h"
#include "sd_card.h"

#include "board_pins.h"

#if defined(SD_IFACE_SPI)
/** @brief SPI speeds to try, fastest first. */
static const uint32_t kBauds[] = {
    12u * 1000 * 1000, 4u * 1000 * 1000, 1u * 1000 * 1000,
    400u * 1000, 200u * 1000,
};
static const uint kSdPins[] = {PIN_SD_MISO, PIN_SD_CS, PIN_SD_SCK, PIN_SD_MOSI};
static const char *kSdNames[] = {"MISO", "CS", "SCK", "MOSI"};
#else
/** @brief SDIO speeds to try, fastest first. */
static const uint32_t kBauds[] = {
    25u * 1000 * 1000, 10u * 1000 * 1000, 4u * 1000 * 1000, 1u * 1000 * 1000,
};
static const uint kSdPins[] = {PIN_SD_SCK, PIN_SD_CMD, PIN_SD_D0,
                               PIN_SD_D1, PIN_SD_D2, PIN_SD_D3};
static const char *kSdNames[] = {"CLK", "CMD", "D0", "D1", "D2", "D3"};
#endif

/**
 * @brief Pin integrity test, without ever engaging an internal pull-down.
 *
 * The obvious test -- pull each way and see if the pin follows -- is wrong on
 * RP2350. Erratum **E9** says a pad configured as an input with the internal
 * pull-down engaged can latch at roughly 2.1 V instead of being pulled to
 * ground, and once latched it reads high no matter what is done to it
 * afterwards. An earlier version of this function did exactly that and reported
 * all six SDIO lines as "held high", which is not a fault any board has; it was
 * the erratum, triggered by the test itself.
 *
 * So this drives the pad instead, which is unambiguous and unaffected:
 *
 *   drive high, read back 0  ->  shorted to ground
 *   drive low,  read back 1  ->  shorted to 3V3
 *   both agree               ->  the pin can be driven, which is all SD needs
 *
 * The pull-up read is kept because it is safe -- E9 concerns the pull-down --
 * and because it distinguishes a floating line from one a card is holding.
 * Every pin is left driven low afterwards rather than floating, so nothing is
 * parked at an intermediate voltage where E9 could bite later.
 *
 * @return true if every pin can be driven both ways.
 */
static bool testPinIntegrity(void) {
    const uint *pins = kSdPins;
    const char **names = kSdNames;
    const unsigned nPins = count_of(kSdPins);
    bool allOk = true;

    printf("\nPin integrity (drive test; no internal pull-downs -- see RP2350-E9):\n");
    printf("  %-9s %-6s %-6s %-6s %s\n", "PIN", "HIGH", "LOW", "PULLUP", "VERDICT");

    for (unsigned i = 0; i < nPins; i++) {
        gpio_init(pins[i]);
        gpio_disable_pulls(pins[i]);

        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 1);
        sleep_us(200);
        int hi = gpio_get(pins[i]) ? 1 : 0;

        gpio_put(pins[i], 0);
        sleep_us(200);
        int lo = gpio_get(pins[i]) ? 1 : 0;

        // Release and let the internal pull-up decide. A card holding the line
        // reads 0 here while still driving cleanly above.
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
        sleep_ms(2);
        int pu = gpio_get(pins[i]) ? 1 : 0;

        const char *verdict;
        if (!hi)      { verdict = "SHORTED TO GND"; allOk = false; }
        else if (lo)  { verdict = "SHORTED TO 3V3"; allOk = false; }
        else if (!pu) verdict = "ok, held low (card?)";
        else          verdict = "ok";

        printf("  %-5s(%2u) %-6d %-6d %-6d %s\n",
               names[i], pins[i], hi, lo, pu, verdict);

        // Leave it driven low rather than floating at an undefined level.
        gpio_disable_pulls(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 0);
    }

    // Hand every pin back as a plain input so the SD driver starts from a known
    // state rather than fighting whatever this function left behind.
    for (unsigned i = 0; i < nPins; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_disable_pulls(pins[i]);
    }
    return allOk;
}

int main(void) {
#ifdef PIN_BAT_EN
    // Same latch the firmware asserts: without it a 2.8" board running on
    // battery switches itself off as soon as the power button is released,
    // which looks exactly like a firmware that crashed on boot.
    gpio_init(PIN_BAT_EN);
    gpio_set_dir(PIN_BAT_EN, GPIO_OUT);
    gpio_put(PIN_BAT_EN, 1);
#endif

    stdio_init_all();
    sleep_ms(3000);            // time to open a terminal

    // Headless by design: no display, no touch, no PIO, no second core. A dark
    // panel is the expected result -- everything comes out here.
    printf("\n\n=== DshotDisplay standalone SD test ===\n");
    printf("(the screen stays black: this test never starts the display)\n");
#if defined(SD_IFACE_SPI)
    printf("interface: hardware SPI1\n");
#else
    printf("interface: PIO SDIO, 4-bit\n");
#endif

    bool pinsOk = testPinIntegrity();
    if (!pinsOk) {
        printf("\n*** A pin could not be driven. ***\n"
               "Run again with the slot EMPTY. If the same pin still fails, the\n"
               "fault is on the board and no firmware change can help. The mount\n"
               "is attempted below regardless -- a pin that drives both ways is\n"
               "all the card actually needs.\n");
    }

    sd_card_t *card = sd_get_by_num(0);
    if (!card) { printf("no card object -- hw_config is not linked\n"); for (;;); }

    for (unsigned i = 0; i < count_of(kBauds); i++) {
        // Force a full re-init at the new speed: the driver caches both the SPI
        // setup and the card state, and neither is revisited on a retry.
#if defined(SD_IFACE_SPI)
        spi_t *spi = spi_get_by_num(0);
        spi->initialized = false;
        spi->baud_rate = kBauds[i];
#else
        card->sdio_if_p->baud_rate = kBauds[i];
#endif
        card->state.m_Status = 0xFF;

        printf("\n--- trying %lu Hz ---\n", (unsigned long)kBauds[i]);
        FATFS fs;
        FRESULT fr = f_mount(&fs, sd_get_drive_prefix(card), 1);
        printf("f_mount -> %d (%s)\n", fr, FRESULT_str(fr));
        printf("card type=%d sectors=%lu\n", (int)card->state.card_type,
               (unsigned long)card->state.sectors);

        if (fr == FR_OK) {
            printf("MOUNTED at %lu Hz\n", (unsigned long)kBauds[i]);
            FIL f;
            if (f_open(&f, "SDTEST.TXT", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
                UINT bw = 0;
                f_write(&f, "DshotDisplay SD test\n", 21, &bw);
                f_close(&f);
                printf("wrote SDTEST.TXT (%u bytes)\n", bw);
            }
            f_unmount(sd_get_drive_prefix(card));
            break;
        }
    }

    printf("\n=== done ===\n");
    for (;;) tight_loop_contents();
}
