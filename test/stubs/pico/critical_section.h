// Host-test stub. Mirrors the signatures of the real header so the
// firmware can be compiled and exercised on a PC. Not used on device.
#pragma once
typedef struct { int lock; } critical_section_t;
extern "C" {
void critical_section_init(critical_section_t*);
void critical_section_enter_blocking(critical_section_t*);
void critical_section_exit(critical_section_t*);
}
