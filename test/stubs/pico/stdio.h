// Host-test stub: the SDK's stdio entry points.
//
// Mirrors the real split deliberately: this header forward-declares the driver
// type and declares the function that registers one; the struct definition is
// in pico/stdio/driver.h, exactly as in the SDK.
#pragma once

#include <stdbool.h>

#define PICO_ERROR_NO_DATA (-2)

#ifndef PICO_STDIO_ENABLE_CRLF_SUPPORT
#define PICO_STDIO_ENABLE_CRLF_SUPPORT 1
#endif
#ifndef PICO_STDIO_DEFAULT_CRLF
#define PICO_STDIO_DEFAULT_CRLF 1
#endif

typedef struct stdio_driver stdio_driver_t;

void stdio_set_driver_enabled(stdio_driver_t *driver, bool enabled);
