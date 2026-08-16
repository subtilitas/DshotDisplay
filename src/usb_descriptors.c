/**
 * @file usb_descriptors.c
 * @brief What the host is told this device is. @see usb_dev.h for why we own these.
 *
 * C rather than C++ because TinyUSB looks these callbacks up with C linkage.
 *
 * The layout is CDC (two interfaces, as the class requires) followed by MSC,
 * and that order is deliberate: hosts that get confused by composite devices
 * get confused less when the CDC pair comes first behind an IAD, which is the
 * arrangement every USB-serial-plus-something device on the market uses.
 */

#include "tusb.h"

#include "config.h"
#include "usb_dev.h"

#include <string.h>

/**
 * @brief Product ID, derived from the interfaces present.
 *
 * TinyUSB's own convention, kept because it is a real convention rather than a
 * habit: a device whose interface set changes should not reuse a PID, or hosts
 * cache a driver binding for the wrong shape and the second enumeration is the
 * broken one. Ours never changes at run time -- both interfaces are always
 * present, and the drive reports "no medium" rather than disappearing -- but
 * encoding it costs nothing and documents the rule for whoever adds a third.
 */
#define USB_PID (0x4000 | (CFG_TUD_CDC << 1) | (CFG_TUD_MSC << 2))

/** @brief Raspberry Pi's vendor ID, as used by every Pico SDK device. */
#define USB_VID 0x2E8A

/** @brief Device descriptor. Class is in the interfaces, not here. */
static const tusb_desc_device_t s_device = {
	.bLength            = sizeof(tusb_desc_device_t),
	.bDescriptorType    = TUSB_DESC_DEVICE,
	.bcdUSB             = 0x0200,

	// Miscellaneous / common class / IAD. Says "read the interface association
	// descriptors" -- the composite-device incantation. Without it Windows binds
	// one driver to the whole device and only one of the two functions works.
	.bDeviceClass       = TUSB_CLASS_MISC,
	.bDeviceSubClass    = MISC_SUBCLASS_COMMON,
	.bDeviceProtocol    = MISC_PROTOCOL_IAD,
	.bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

	.idVendor           = USB_VID,
	.idProduct          = USB_PID,
	.bcdDevice          = 0x0100,

	.iManufacturer      = 0x01,
	.iProduct           = 0x02,
	.iSerialNumber      = 0x03,

	.bNumConfigurations = 0x01,
};

const uint8_t *tud_descriptor_device_cb(void) {
	return (const uint8_t *)&s_device;
}

/** @brief Interface numbers. MSC follows the CDC pair. */
enum {
	ITF_NUM_CDC = 0,
	ITF_NUM_CDC_DATA,
	ITF_NUM_MSC,
	ITF_NUM_TOTAL,
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

/**
 * @defgroup usbdesc_ep Endpoint addresses
 * @brief Hand-assigned, and they must not collide.
 *
 * RP2350 USB has a limited set of endpoints and TinyUSB does not allocate them
 * for you. A collision does not fail to build; it enumerates and then one
 * function silently does nothing, which is a long afternoon.
 * @{
 */
#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x82
#define EPNUM_MSC_OUT   0x03
#define EPNUM_MSC_IN    0x83
/** @} */

static const uint8_t s_config[] = {
	// Config: interface count, string index, total length, attributes, mA.
	TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
	                      TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

	// CDC: interface number, string index, notification EP, its size, data EPs.
	TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8,
	                   EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

	// MSC: interface number, string index, endpoints, endpoint size.
	TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 5, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
	(void)index;
	return s_config;
}

/**
 * @brief String descriptors.
 *
 * Index 3 is the serial number and is filled in at run time from the chip's
 * unique ID, so that two of these boards on one machine are told apart. A fixed
 * serial makes the host reuse the first one's drive letter and mount state for
 * the second, which looks like the second board being broken.
 */
static const char *const s_strings[] = {
	(const char[]){0x09, 0x04},  // 0: language, en-US
	"subtilitas",                // 1: manufacturer
	"DshotDisplay",              // 2: product
	"000000000000",              // 3: serial, overwritten by usbDescSetSerial()
	"DshotDisplay Serial",       // 4: CDC interface
	"DshotDisplay SD Card",      // 5: MSC interface
};

/** @brief The serial-number string, as ASCII. @see usbDescSetSerial() */
static char s_serial[17] = "000000000000";

void usbDescSetSerial(const char *ascii) {
	size_t i = 0;
	for (; ascii[i] && i < sizeof(s_serial) - 1; i++) s_serial[i] = ascii[i];
	s_serial[i] = 0;
}

/** @brief UTF-16LE scratch for the descriptor TinyUSB asks for. */
static uint16_t s_descStr[32];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
	(void)langid;
	size_t chars;

	if (index == 0) {
		memcpy(&s_descStr[1], s_strings[0], 2);
		chars = 1;
	} else {
		if (index >= sizeof(s_strings) / sizeof(s_strings[0])) return NULL;

		const char *str = (index == 3) ? s_serial : s_strings[index];

		chars = strlen(str);
		// Bounded by the scratch buffer, minus the header word. A string longer
		// than this is truncated rather than written past the end.
		if (chars > sizeof(s_descStr) / sizeof(s_descStr[0]) - 1)
			chars = sizeof(s_descStr) / sizeof(s_descStr[0]) - 1;

		for (size_t i = 0; i < chars; i++) s_descStr[1 + i] = (uint16_t)str[i];
	}

	// First word is length in bytes (including itself) and the descriptor type.
	s_descStr[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chars + 2));
	return s_descStr;
}
