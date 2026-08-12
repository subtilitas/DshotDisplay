// Host-test stub for the SD driver's card object.
//
// Only what sd_log.cpp reads back after a mount attempt: the card type and
// size, which are how "nothing answered on the bus" is told apart from "card
// works, filesystem unreadable".
#pragma once

#include "hw_config.h"

typedef enum {
	SDCARD_NONE = 0, SDCARD_V1 = 1, SDCARD_V2 = 2, SDCARD_V2HC = 3,
} card_type_t;

const char *sd_get_drive_prefix(sd_card_t *sd_card_p);
