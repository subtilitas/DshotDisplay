/**
 * @file usb_dev.cpp
 * @brief TinyUSB bring-up and the serial port that pico_stdio_usb used to give us.
 *
 * @see usb_dev.h for why this file exists rather than one line of CMake.
 */

#include "usb_dev.h"
#include "config.h"

#include "tusb.h"

#include <pico/stdio.h>
#include <pico/unique_id.h>
#include <stdio.h>
#include <string.h>

/** @brief Fill the serial-number string descriptor. @see usb_descriptors.c */
extern "C" void usbDescSetSerial(const char *ascii);

// ---------------------------------------------------------------------------
// stdio over CDC
// ---------------------------------------------------------------------------

/**
 * @brief Write to the serial port, dropping rather than blocking.
 *
 * The blocking question is the whole design of this function, and the answer is
 * forced by what else runs on this core. printf() is called from the AM32
 * transport and the SD driver's tracing, both on core0, and core0 must call
 * escHeartbeat() every UI frame or core1 decides the display has hung and zeroes
 * the throttle. A printf that waited for a host that is not reading -- a serial
 * port opened and then left, which is the normal state of a bench board -- would
 * stall core0 past that timeout.
 *
 * So a full buffer drops. Debug output is not worth a motor stopping, and it is
 * certainly not worth a stall that looks like a firmware hang.
 */
static void cdcOutChars(const char *buf, int len) {
	if (!tud_cdc_connected()) return;

	for (int i = 0; i < len;) {
		uint32_t room = tud_cdc_write_available();
		if (room == 0) {
			// One flush attempt, then give up on the rest of this call.
			tud_cdc_write_flush();
			room = tud_cdc_write_available();
			if (room == 0) return;
		}
		uint32_t n = (uint32_t)(len - i);
		if (n > room) n = room;
		tud_cdc_write(buf + i, n);
		i += (int)n;
	}
	tud_cdc_write_flush();
}

/** @brief stdin is not used. Reported empty so getchar() cannot block. */
static int cdcInChars(char *buf, int len) {
	(void)buf; (void)len;
	return PICO_ERROR_NO_DATA;
}

/**
 * @brief The SDK's stdio hook, pointed at the CDC interface we describe.
 *
 * `crlf_enabled` matches what pico_stdio_usb defaulted to, so terminals behave
 * exactly as they did before this file existed.
 */
static stdio_driver_t s_cdcDriver = {
	.out_chars = cdcOutChars,
	.out_flush = nullptr,
	.in_chars  = cdcInChars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
	.crlf_enabled = PICO_STDIO_DEFAULT_CRLF,
#endif
};

// ---------------------------------------------------------------------------
// device
// ---------------------------------------------------------------------------

void usbDevInit() {
	// A serial number the host can tell two boards apart by. The chip's unique
	// ID, formatted as hex, which is what every Pico SDK device does.
	char id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
	pico_get_unique_board_id_string(id, sizeof(id));
	usbDescSetSerial(id);

	tusb_init();
	stdio_set_driver_enabled(&s_cdcDriver, true);
}

void usbDevTask() { tud_task(); }

bool usbDevMounted() { return tud_mounted(); }
