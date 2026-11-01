#ifndef TEST_STDLIB_H
#define TEST_STDLIB_H
#include <stdbool.h>
#include <stdint.h>
typedef unsigned int uint;
typedef uint64_t absolute_time_t;
#define GPIO_IN 0
void stdio_init_all(void);
bool stdio_usb_connected(void);
absolute_time_t get_absolute_time(void);
uint32_t to_ms_since_boot(absolute_time_t t);
void gpio_init(uint pin);
void gpio_set_dir(uint pin, bool out);
void gpio_pull_up(uint pin);
bool gpio_get(uint pin);
#endif
