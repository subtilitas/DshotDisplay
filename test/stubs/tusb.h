// Host-test stub for TinyUSB.
//
// Enough of the API for src/usb_dev.cpp, src/usb_descriptors.c and
// src/usb_msc.cpp to typecheck under `make syntax`. None of it runs: there is
// no USB on the host, and the parts of the feature that *can* be reasoned about
// on a host are the pure rules in usb_msc.h, which test_usb_msc.cpp covers.
//
// What this buys is still worth having. It is the same deal the PIO and SD
// stubs already make: a signature that drifts -- a callback TinyUSB renames, an
// argument that changes type -- fails here, on the machine you are already on,
// instead of after a CI round-trip to an ARM toolchain.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- config, as tusb_config.h expects the real headers to have defined ---
#define OPT_MCU_RP2040        1
#define OPT_OS_PICO           1
#define OPT_MODE_FULL_SPEED   0x0400
#define OPT_MODE_DEVICE       0x0001

#include "tusb_config.h"

// --- descriptors ---
#define TUSB_DESC_DEVICE      0x01
#define TUSB_DESC_CONFIG      0x02
#define TUSB_DESC_STRING      0x03

#define TUSB_CLASS_MISC       0xEF
#define MISC_SUBCLASS_COMMON  0x02
#define MISC_PROTOCOL_IAD     0x01

#define TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP 0x20

typedef struct __attribute__((packed)) {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t bcdUSB;
	uint8_t  bDeviceClass;
	uint8_t  bDeviceSubClass;
	uint8_t  bDeviceProtocol;
	uint8_t  bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t  iManufacturer;
	uint8_t  iProduct;
	uint8_t  iSerialNumber;
	uint8_t  bNumConfigurations;
} tusb_desc_device_t;

// The real macros expand to comma-separated descriptor bytes. These expand to
// one byte each, which is wrong in value and right in shape: the array still
// compiles and the *_DESC_LEN constants below keep the length arithmetic
// honest, which is all a syntax check can check.
#define TUD_CONFIG_DESCRIPTOR(...) 0
#define TUD_CDC_DESCRIPTOR(...)    0
#define TUD_MSC_DESCRIPTOR(...)    0

#define TUD_CONFIG_DESC_LEN   9
#define TUD_CDC_DESC_LEN      66
#define TUD_MSC_DESC_LEN      23

// --- device ---
bool tusb_init(void);
void tud_task(void);
bool tud_mounted(void);

// --- CDC ---
bool     tud_cdc_connected(void);
uint32_t tud_cdc_write_available(void);
uint32_t tud_cdc_write(const void *buffer, uint32_t bufsize);
uint32_t tud_cdc_write_flush(void);

// --- MSC ---
#define SCSI_SENSE_NOT_READY        0x02
#define SCSI_SENSE_MEDIUM_ERROR     0x03
#define SCSI_SENSE_ILLEGAL_REQUEST  0x05
#define SCSI_SENSE_DATA_PROTECT     0x07

bool tud_msc_set_sense(uint8_t lun, uint8_t sense_key, uint8_t add_sense_code,
                       uint8_t add_sense_qualifier);
