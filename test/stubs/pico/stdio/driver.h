// Host-test stub: the SDK's stdio driver struct.
//
// A separate header because that is where the SDK keeps it -- pico/stdio.h only
// forward-declares the typedef -- and the first version of this stub put the
// full definition in pico/stdio.h instead. That built here and failed on the
// real toolchain with "variable has initializer but incomplete type", which is
// exactly the class of error a stub exists to catch rather than to hide.
//
// The member list mirrors the SDK's, including the two this project does not
// set: designated initialisers must follow declaration order, so a stub missing
// a member in the middle would accept an ordering the real header rejects.
#pragma once

#include "pico/stdio.h"

struct stdio_driver {
	void (*out_chars)(const char *buf, int len);
	void (*out_flush)(void);
	int  (*in_chars)(char *buf, int len);
	void (*set_chars_available_callback)(void (*fn)(void *), void *param);
	stdio_driver_t *next;
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
	bool last_ended_with_cr;
	bool crlf_enabled;
#endif
};
