#ifndef TEST_STDLIB_H
#define TEST_STDLIB_H
#include <stdbool.h>
#include <stdint.h>
typedef unsigned int uint;
typedef uint64_t absolute_time_t;
#define _u(x) x##u
#define count_of(a) (sizeof(a)/sizeof((a)[0]))
absolute_time_t make_timeout_time_us(uint64_t us);
#define GPIO_IN 0
#define GPIO_FUNC_I2C 3
void gpio_set_function(uint pin, uint func);
void sleep_ms(uint32_t ms);
#define NUM_BANK0_GPIOS 30
void sleep_us(uint64_t us);
void tight_loop_contents(void);
uint64_t time_us_64(void);
void stdio_init_all(void);
bool stdio_usb_connected(void);
absolute_time_t get_absolute_time(void);
uint32_t to_ms_since_boot(absolute_time_t t);
void gpio_init(uint pin);
void gpio_set_dir(uint pin, bool out);
void gpio_pull_up(uint pin);
bool gpio_get(uint pin);
#endif
