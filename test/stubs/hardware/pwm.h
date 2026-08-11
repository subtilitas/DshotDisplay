// Host-test stub for hardware/pwm.h.
#pragma once
#include <stdint.h>
typedef struct { uint32_t csr, div, top; } pwm_config;
unsigned pwm_gpio_to_slice_num(unsigned gpio);
pwm_config pwm_get_default_config();
void pwm_config_set_clkdiv(pwm_config *c, float div);
void pwm_config_set_wrap(pwm_config *c, uint16_t wrap);
void pwm_init(unsigned slice, pwm_config *c, bool start);
void pwm_set_gpio_level(unsigned gpio, uint16_t level);
