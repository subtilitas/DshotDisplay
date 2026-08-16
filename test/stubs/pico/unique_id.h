// Host-test stub: the chip's unique ID, used for the USB serial number.
#pragma once
#include <stddef.h>
#define PICO_UNIQUE_BOARD_ID_SIZE_BYTES 8
void pico_get_unique_board_id_string(char *id_out, unsigned int len);
