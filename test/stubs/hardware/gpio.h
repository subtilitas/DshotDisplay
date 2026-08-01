// Host-test stub. Mirrors the signatures of the real header so the
// firmware can be compiled and exercised on a PC. Not used on device.
#pragma once
#include <stdint.h>
#define GPIO_OUT 1
#define GPIO_IN 0
enum gpio_function { GPIO_FUNC_SPI = 1 };
extern "C" {
void gpio_init(uint32_t);
void gpio_set_dir(uint32_t, int);
void gpio_put(uint32_t, int);
void gpio_set_function(uint32_t, gpio_function);
bool gpio_get(uint32_t);
void gpio_pull_up(uint32_t);
void gpio_disable_pulls(uint32_t);
}
