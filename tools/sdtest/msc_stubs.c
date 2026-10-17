/*
 * SPDX-License-Identifier: MIT
 */

/**
 * @file msc_stubs.c
 * @brief Mass-storage callbacks sdtest never uses, so that it links.
 *
 * sdtest has src/ on its include path for the board descriptors, so TinyUSB
 * finds the firmware's tusb_config.h, which turns on the MSC (mass storage
 * class) driver. That driver needs these callbacks at link time. sdtest's
 * USB descriptors come from pico_stdio_usb and offer no MSC interface, so a
 * host never reaches them. In case one does, test_unit_ready answers not
 * ready, the capacity is 0 blocks, and read10, write10 and every SCSI
 * command outside TinyUSB's built-in set answer with an error.
 */

#include <string.h>

#include "tusb.h"

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16],
                        uint8_t product_rev[4]) {
	(void)lun;
	memcpy(vendor_id, "sdtest  ", 8);
	memcpy(product_id, "none            ", 16);
	memcpy(product_rev, "0   ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
	(void)lun;
	return false;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
	(void)lun;
	*block_count = 0;
	*block_size = 512;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer,
                          uint32_t bufsize) {
	(void)lun; (void)lba; (void)offset; (void)buffer; (void)bufsize;
	return -1;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer,
                           uint32_t bufsize) {
	(void)lun; (void)lba; (void)offset; (void)buffer; (void)bufsize;
	return -1;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
	(void)scsi_cmd; (void)buffer; (void)bufsize;
	tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
	return -1;
}
