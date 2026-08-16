// Host-test stub: the SDK's stdio driver hook, which usb_dev.cpp registers a
// CDC-backed driver with in place of pico_stdio_usb.
#pragma once
#include <stdbool.h>

#define PICO_ERROR_NO_DATA (-2)
#ifndef PICO_STDIO_ENABLE_CRLF_SUPPORT
#define PICO_STDIO_ENABLE_CRLF_SUPPORT 1
#endif
#ifndef PICO_STDIO_DEFAULT_CRLF
#define PICO_STDIO_DEFAULT_CRLF 1
#endif

typedef struct stdio_driver {
	void (*out_chars)(const char *buf, int len);
	void (*out_flush)(void);
	int  (*in_chars)(char *buf, int len);
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
	bool crlf_enabled;
#endif
} stdio_driver_t;

void stdio_set_driver_enabled(stdio_driver_t *driver, bool enabled);
