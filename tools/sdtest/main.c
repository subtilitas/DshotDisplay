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
 * @brief Pin integrity test: pull each SD line both ways, then drive it.
 *
 * A single pulled-up reading cannot tell a stuck pin from a card holding a line
 * down. Pulling both ways can:
 *
 *   up=1 down=0   floating. Normal for an empty slot, and for a powered card
 *                 that is not driving the line.
 *   up=0 down=0   held low. A short to ground, a dead pin, or an unpowered card
 *                 clamping through its ESD diode.
 *   up=1 down=1   held high. A short to 3V3.
 *
 * Then it drives each pin high as an output and reads back. A pin that cannot
 * be driven high is shorted to ground, and no amount of SPI configuration will
 * make the card hear a command on it.
 *
 * @return true if every pin looks sane.
 */
static bool testPinIntegrity(void) {
    const uint pins[] = {PIN_SD_MISO, PIN_SD_CS, PIN_SD_SCK, PIN_SD_MOSI};
    const char *names[] = {"MISO(24)", "CS(25)", "SCK(26)", "MOSI(27)"};
    bool allOk = true;

    printf("\nPin integrity, SPI detached:\n");
    printf("  %-9s %-4s %-6s %-6s %s\n", "PIN", "UP", "DOWN", "DRIVE", "VERDICT");

    for (unsigned i = 0; i < count_of(pins); i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);

        gpio_pull_up(pins[i]);
        sleep_ms(2);
        int up = gpio_get(pins[i]) ? 1 : 0;

        gpio_disable_pulls(pins[i]);
        gpio_pull_down(pins[i]);
        sleep_ms(2);
        int down = gpio_get(pins[i]) ? 1 : 0;

        // Drive it high and read back. This is the test that matters: it is the
        // difference between "nothing is pulling it up" and "something is
        // actively holding it down".
        gpio_disable_pulls(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 1);
        sleep_ms(2);
        int driven = gpio_get(pins[i]) ? 1 : 0;
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_disable_pulls(pins[i]);

        const char *verdict;
        if (!driven)            { verdict = "SHORTED TO GND"; allOk = false; }
        else if (up && !down)   verdict = "floating, ok";
        else if (!up && !down)  { verdict = "held low"; allOk = false; }
        else if (up && down)    { verdict = "held high"; allOk = false; }
        else                    verdict = "?";

        printf("  %-9s %-4d %-6d %-6d %s\n", names[i], up, down, driven, verdict);
    }
    return allOk;
}

int main(void) {
    stdio_init_all();
    sleep_ms(3000);            // time to open a terminal

    printf("\n\n=== DshotDisplay standalone SD test ===\n");
    printf("MISO=%d CS=%d SCK=%d MOSI=%d, SPI1\n",
           PIN_SD_MISO, PIN_SD_CS, PIN_SD_SCK, PIN_SD_MOSI);

    bool pinsOk = testPinIntegrity();
    if (!pinsOk) {
        printf("\n*** A pin failed the integrity test. ***\n"
               "Run this again with the slot EMPTY. If the same pin still fails,\n"
               "the fault is on the board -- a short or a damaged pin -- and no\n"
               "firmware change can help. If it passes with the slot empty, the\n"
               "card or the socket contacts are holding the line.\n");
    }

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
