// Host-test stub for the header pioasm generates from src/pio_uart_rx.pio.
//
// On device that header is written into the build tree by
// pico_generate_pio_header() and carries the assembled program. Nothing here
// needs the instructions -- pio_uart_rx.cpp is typechecked, never linked -- so
// this declares the two symbols the generator exports and stops there.
#pragma once
#include "hardware/pio.h"

extern const pio_program_t pio_uart_rx_program;
pio_sm_config pio_uart_rx_program_get_default_config(uint offset);
