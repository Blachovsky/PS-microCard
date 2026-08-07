#ifndef TEST_SUPPORT_PICO_STDLIB_H
#define TEST_SUPPORT_PICO_STDLIB_H

#include <stdbool.h>
#include <stdint.h>

/* Host-test replacements for the Pico SDK pieces used by this module. */
#define __not_in_flash_func(function_name) function_name

typedef unsigned int uint;

#define GPIO_IN  false
#define GPIO_OUT true

int gpio_get(uint gpio);
void gpio_put(uint gpio, bool value);
void gpio_set_dir(uint gpio, bool out);
uint32_t time_us_32(void);
void tight_loop_contents(void);
void busy_wait_us_32(uint32_t delay_us);

#endif // TEST_SUPPORT_PICO_STDLIB_H
