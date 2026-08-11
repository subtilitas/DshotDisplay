// Host-test stub for hardware/adc.h.
#pragma once
#include <stdint.h>
void adc_init();
void adc_gpio_init(unsigned gpio);
void adc_select_input(unsigned input);
uint16_t adc_read();
