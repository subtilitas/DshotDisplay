// Host-test stub for hardware/flash.h.
//
// settings_flash.cpp is never linked into the host suite -- it is in
// DEVICE_SRCS and only typechecked -- so nothing here is ever called. The
// declarations exist so that -fsyntax-only sees the same signatures the SDK
// offers, which is the whole point of typechecking it at all.
//
// The RAM-backed storage the tests actually run against lives in fakes.cpp, as
// settingsStorageRead()/settingsStorageWrite().
#pragma once
#include <stdint.h>
#include <stddef.h>

#define FLASH_PAGE_SIZE   256u
#define FLASH_SECTOR_SIZE 4096u
#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (4u * 1024u * 1024u)
#endif

extern "C" {
void flash_range_erase(uint32_t flash_offs, size_t count);
void flash_range_program(uint32_t flash_offs, const uint8_t *data, size_t count);
}
