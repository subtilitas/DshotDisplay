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

/** @brief Speeds to try, fastest first. */
static const uint32_t kBauds[] = {
    12u * 1000 * 1000, 4u * 1000 * 1000, 1u * 1000 * 1000,
    400u * 1000, 200u * 1000,
};

/**
 * @brief Report the idle level of every SD pin with SPI detached.
 *
 * A floating MISO reads high through the internal pull-up when a card is
 * present but silent, and stays low if the line is shorted or the pin is not
 * what we think it is. This is the cheapest way to tell a dead bus from a
 * dead card without a meter.
 */
static void reportIdleLevels(void) {
    const uint pins[] = {PIN_SD_MISO, PIN_SD_CS, PIN_SD_SCK, PIN_SD_MOSI};
    const char *names[] = {"MISO(24)", "CS(25)", "SCK(26)", "MOSI(27)"};

    printf("\nPin idle levels, SPI detached, internal pull-up on:\n");
    for (unsigned i = 0; i < count_of(pins); i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
        sleep_ms(2);
        printf("  %-9s = %d\n", names[i], gpio_get(pins[i]) ? 1 : 0);
    }
    printf("  MISO 0 with a card fitted means the card is holding it low,\n"
           "  or the pin is not the one the schematic says.\n");
}

int main(void) {
    stdio_init_all();
    sleep_ms(3000);            // time to open a terminal

    printf("\n\n=== DshotDisplay standalone SD test ===\n");
    printf("MISO=%d CS=%d SCK=%d MOSI=%d, SPI1\n",
           PIN_SD_MISO, PIN_SD_CS, PIN_SD_SCK, PIN_SD_MOSI);

    reportIdleLevels();

    sd_card_t *card = sd_get_by_num(0);
    if (!card) { printf("no card object -- hw_config is not linked\n"); for (;;); }

    for (unsigned i = 0; i < count_of(kBauds); i++) {
        // Force a full re-init at the new speed: the driver caches both the SPI
        // setup and the card state, and neither is revisited on a retry.
        spi_t *spi = spi_get_by_num(0);
        spi->initialized = false;
        spi->baud_rate = kBauds[i];
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
