// Host-test stub. Mirrors the signatures of the real header so the
// firmware can be compiled and exercised on a PC. Not used on device.
//
// Declarations only, with no definitions anywhere: the PIO users here are
// device sources that the host suite typechecks (`make syntax`) and never
// links, so a body would be dead weight that could drift from the SDK's.
#pragma once
#include <stdint.h>
typedef struct pio_hw *PIO;
extern PIO pio0;
extern PIO pio1;
#define NUM_PIOS 3

typedef unsigned int uint;

struct pio_program {
	const uint16_t *instructions;
	uint8_t length;
	int8_t origin;
};
typedef struct pio_program pio_program_t;

// Opaque to everything here: the SDK builds it through the sm_config_* setters
// below and hands it straight back to pio_sm_init().
typedef struct pio_sm_config { uint32_t clkdiv, execctrl, shiftctrl, pinctrl; } pio_sm_config;

enum pio_fifo_join {
	PIO_FIFO_JOIN_NONE = 0,
	PIO_FIFO_JOIN_TX = 1,
	PIO_FIFO_JOIN_RX = 2,
};

bool pio_claim_free_sm_and_add_program_for_gpio_range(const pio_program_t *program,
                                                     PIO *pio, uint *sm, uint *offset,
                                                     uint gpio_base, uint gpio_count,
                                                     bool set_gpio_base);
void pio_remove_program_and_unclaim_sm(const pio_program_t *program, PIO pio,
                                       uint sm, uint offset);

void sm_config_set_in_pins(pio_sm_config *c, uint in_base);
void sm_config_set_jmp_pin(pio_sm_config *c, uint pin);
void sm_config_set_in_shift(pio_sm_config *c, bool shift_right, bool autopush,
                            uint push_threshold);
void sm_config_set_fifo_join(pio_sm_config *c, enum pio_fifo_join join);
void sm_config_set_clkdiv(pio_sm_config *c, float div);

void pio_gpio_init(PIO pio, uint pin);
void pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint pin_base, uint pin_count,
                                    bool is_out);
void pio_sm_init(PIO pio, uint sm, uint initial_pc, const pio_sm_config *config);
void pio_sm_set_enabled(PIO pio, uint sm, bool enabled);
void pio_sm_clear_fifos(PIO pio, uint sm);
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm);
uint32_t pio_sm_get(PIO pio, uint sm);
