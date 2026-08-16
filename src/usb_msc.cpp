/**
 * @file usb_msc.cpp
 * @brief The card reader half: TinyUSB MSC callbacks over the SD block driver.
 *
 * The rules this obeys are in usb_msc.h and are tested there. What is here is
 * the I/O and the transitions the rules ask for, and the one property it must
 * never break: while mscHoldsCard() says the card is spoken for, nothing in
 * this firmware touches the filesystem. @see msc_exclusive
 */

#include "usb_msc.h"
#include "sd_log.h"
#include "config.h"
#include "ui.h"

#include "tusb.h"
#include "hw_config.h"
#include "sd_card.h"

#include "plat.h"
#include <string.h>

/** @brief Where the handover has got to. Core0 only. */
static MscState  s_state = MscState::Idle;
/** @brief Blocks the host has read since the medium last appeared. */
static uint32_t  s_blocksRead = 0;
/** @brief Sector count of the card, latched when the medium is offered. */
static uint32_t  s_sectors = 0;

/** @brief The card, or null if the driver has none. */
static sd_card_t *card() { return sd_get_by_num(0); }

/**
 * @brief Sectors on the card, asked of the driver.
 *
 * Both drivers fill `state.sectors` after a successful init, and the SDIO one
 * needs the accessor when it has not. Same two-step sd_log.cpp does, and for
 * the same reason: reading one field alone reports zero for a card that is
 * demonstrably working.
 */
static uint32_t cardSectors() {
	sd_card_t *c = card();
	if (!c) return 0;
	uint32_t n = c->state.sectors;
	if (!n && c->get_num_sectors) n = c->get_num_sectors(c);
	return n;
}

void mscInit() {
	s_state = MscState::Idle;
	s_blocksRead = 0;
	s_sectors = 0;
}

MscState mscGetState() { return s_state; }
uint32_t mscBlocksRead() { return s_blocksRead; }

MscRefusal mscRequest() {
	SdLogStatus st;
	sdLogStatus(&st);

	MscRefusal r = mscRefusal(st.state != SdLogState::NoCard, uiArmed(),
	                          st.state == SdLogState::Logging);
	if (r != MscRefusal::None) return r;

	s_state = mscNext(s_state, MscEvent::Request);
	return MscRefusal::None;
}

void mscRelease() {
	s_state = mscNext(s_state, MscEvent::Release);
}

void mscTick() {
	switch (s_state) {
		case MscState::Handover: {
			// Give the card up before the medium is ever reported present. The
			// order matters more than it looks: a host that sees the medium
			// appear may start reading within a millisecond, and anything this
			// side still had open would be read half-written.
			if (!sdLogRelease()) {
				// A log opened between the request and now. Back out rather
				// than serve a filesystem somebody is writing to.
				s_state = mscNext(s_state, MscEvent::Release);
				break;
			}
			s_sectors = cardSectors();
			if (!s_sectors) {
				// Released a card that turns out not to answer. Nothing to
				// serve; take it straight back.
				s_state = mscNext(s_state, MscEvent::CardGone);
				sdLogReacquire();
				break;
			}
			s_blocksRead = 0;
			s_state = mscNext(s_state, MscEvent::Done);
			break;
		}

		case MscState::Reclaim:
			s_sectors = 0;
			sdLogReacquire();
			s_state = mscNext(s_state, MscEvent::Done);
			break;

		case MscState::Serving:
		case MscState::Idle:
			break;
	}
}

// ---------------------------------------------------------------------------
// TinyUSB MSC callbacks. Called from tud_task(), which is core0.
// ---------------------------------------------------------------------------

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16],
                        uint8_t product_rev[4]) {
	(void)lun;
	// Fixed-width, space-padded, not NUL-terminated: SCSI INQUIRY fields are
	// blank-filled ASCII, and a NUL in them shows up as a mangled device name in
	// Windows' device manager.
	memcpy(vendor_id,   "subtilit", 8);
	memcpy(product_id,  "DshotDisplay SD ", 16);
	memcpy(product_rev, "1.0 ", 4);
}

/**
 * @brief Is there a medium in the reader?
 *
 * This one callback is the whole "press the button to insert the card"
 * illusion. Outside @ref MscState::Serving it answers no, with the sense code a
 * real reader gives for an empty slot, and every host already knows what that
 * means: show the drive, do not nag.
 */
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
	(void)lun;
	if (s_state == MscState::Serving && s_sectors) return true;

	// 0x02 NOT READY / 0x3A00 MEDIUM NOT PRESENT.
	tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
	return false;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
	(void)lun;
	*block_count = (s_state == MscState::Serving) ? s_sectors : 0;
	*block_size  = 512;
}

/**
 * @brief START STOP UNIT: how a host says "eject".
 *
 * Honouring it is what makes the operating system's own eject work, which is
 * the gesture people already know. The screen's EJECT button and this end up in
 * the same place.
 */
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start,
                           bool load_eject) {
	(void)lun; (void)power_condition;
	if (load_eject && !start) s_state = mscNext(s_state, MscEvent::HostEject);
	return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize) {
	(void)lun;
	if (s_state != MscState::Serving) return -1;
	if (lba >= s_sectors) return -1;

	sd_card_t *c = card();
	if (!c) return -1;

	// Whole blocks only. offset is non-zero only if the host asked for part of
	// one, which requires a block buffer we would have to justify the RAM for;
	// refusing is honest and no host does it for a read of a FAT volume.
	if (offset != 0 || (bufsize % 512u) != 0) return -1;

	uint32_t blocks = bufsize / 512u;
	if (lba + blocks > s_sectors) blocks = s_sectors - lba;
	if (!blocks) return 0;

	if (c->read_blocks(c, (uint8_t *)buffer, lba, blocks) != SD_BLOCK_DEVICE_ERROR_NONE) {
		// 0x03 MEDIUM ERROR / 0x1100 UNRECOVERED READ ERROR. Reported rather
		// than returning a short read, so the host's copy fails loudly instead
		// of producing a file full of whatever was in the buffer.
		tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
		return -1;
	}

	s_blocksRead += blocks;
	return (int32_t)(blocks * 512u);
}

/**
 * @brief Writes are refused, and refused the way the standard says to.
 *
 * A write-protect sense code makes hosts report "the disk is write protected",
 * which is a sentence users understand. Returning a plain error instead makes
 * Windows retry, then declare the drive corrupt and offer to format it -- an
 * offer nobody should be shown about a card full of logs.
 */
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t bufsize) {
	(void)lba; (void)offset; (void)buffer; (void)bufsize;
	// 0x07 DATA PROTECT / 0x2700 WRITE PROTECTED.
	tud_msc_set_sense(lun, SCSI_SENSE_DATA_PROTECT, 0x27, 0x00);
	return -1;
}

/** @brief Also asked directly, before the host tries a write. */
bool tud_msc_is_writable_cb(uint8_t lun) {
	(void)lun;
	return false;
}

/**
 * @brief SCSI commands the class driver does not handle itself.
 *
 * Everything we care about is handled above. Anything else is refused with
 * "invalid command", which is what a device that does not implement a command
 * is supposed to say.
 */
int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t scsi_cmd[16], void *buffer,
                        uint16_t bufsize) {
	(void)scsi_cmd; (void)buffer; (void)bufsize;
	// 0x05 ILLEGAL REQUEST / 0x2000 INVALID COMMAND OPERATION CODE.
	tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
	return -1;
}
