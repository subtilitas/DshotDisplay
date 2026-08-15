/**
 * @file settings_flash.cpp
 * @brief The one flash sector the settings live in. Device only.
 *
 * Split from settings.cpp so the rules in that file can be host-tested; see
 * @ref settings_storage. Nothing here has any logic worth testing — it is the
 * RP2350 erase-and-program sequence and the two guards that stop it landing
 * somewhere it should not.
 *
 * @section settings_flash_where Where the sector is
 *
 * The last sector of the address space the build declares, which is
 * `PICO_FLASH_SIZE_BYTES` — 4 MB under `PICO_BOARD=pico2`, whatever a custom
 * board header says otherwise. Deliberately not the last sector of the
 * *physical* part: the 2.0" board carries 16 MB and the 2.8" need not, and a
 * settings area whose address depends on which chip was fitted is one that
 * moves when Waveshare changes a supplier.
 *
 * @section settings_flash_safety The two guards
 *
 * - **The firmware must not have grown into it.** `__flash_binary_end` is the
 *   end of the image, published by the SDK linker script. If it ever reaches
 *   the sector, saving is refused rather than overwriting code. That is a
 *   runtime check because there is no link-time one: the image and this address
 *   are decided by different parts of the build.
 * - **Core1 must be parked.** `flash_range_erase()` disables XIP, so any core
 *   executing from flash while it runs fetches from a bus that is not
 *   answering. Core1 runs the DShot pump straight out of flash, so it has to be
 *   held in `multicore_lockout` for the duration — which is also why saving is
 *   only ever offered while disarmed.
 */

#include "settings.h"

#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/multicore.h>
#include <string.h>

/** @brief End of the linked image, from the SDK linker script. */
extern "C" char __flash_binary_end;

/** @brief Byte offset of the settings sector from the start of flash. */
static uint32_t sectorOffset() {
	return (uint32_t)(PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE);
}

/**
 * @brief Where the sector is readable through XIP.
 *
 * Via `uintptr_t` rather than casting the 32-bit sum straight to a pointer:
 * that is exact on the device and a warning on the 64-bit host, and the host is
 * where this file gets typechecked with warnings fatal.
 *
 * @return Pointer to the start of the settings sector.
 */
static const void *sectorAddr() {
	uintptr_t base = (uintptr_t)XIP_BASE;
	return (const void *)(base + (uintptr_t)sectorOffset());
}

/**
 * @brief True if the firmware image ends before the settings sector.
 *
 * @return false when the image has grown into the sector, in which case saving
 *         is refused. Loading still works: whatever is there is either a valid
 *         block or fails its CRC, and both outcomes are safe.
 */
static bool sectorIsFree() {
	uint32_t imageEnd = (uint32_t)((uintptr_t)&__flash_binary_end - XIP_BASE);
	return imageEnd <= sectorOffset();
}

bool settingsStorageRead(void *dst, uint32_t len) {
	if (len > FLASH_SECTOR_SIZE) return false;
	// Flash is memory-mapped through XIP, so a read is a plain memcpy and needs
	// none of the ceremony a write does.
	memcpy(dst, sectorAddr(), len);
	return true;
}

bool settingsStorageWrite(const void *src, uint32_t len) {
	if (len > FLASH_SECTOR_SIZE) return false;
	if (!sectorIsFree()) return false;

	// Programming happens a page at a time and a page must be written whole, so
	// the block is staged into a page-sized buffer first. One page is enough:
	// SettingsBlock is a couple of dozen bytes and a static_assert keeps it so.
	static_assert(sizeof(SettingsBlock) <= FLASH_PAGE_SIZE,
	              "SettingsBlock must fit one flash page");
	uint8_t page[FLASH_PAGE_SIZE];
	memset(page, 0xFF, sizeof(page));
	memcpy(page, src, len);

	// Park core1 before touching flash: the erase turns XIP off, and core1 is
	// executing from XIP. Core1 opts in to being parked in setup1().
	multicore_lockout_start_blocking();
	uint32_t ints = save_and_disable_interrupts();

	flash_range_erase(sectorOffset(), FLASH_SECTOR_SIZE);
	flash_range_program(sectorOffset(), page, FLASH_PAGE_SIZE);

	restore_interrupts(ints);
	multicore_lockout_end_blocking();
	return true;
}
