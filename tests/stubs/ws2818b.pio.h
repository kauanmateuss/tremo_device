#ifndef TEST_WS2818B_H
#define TEST_WS2818B_H
#include "hardware/pio.h"
static const struct pio_program ws2818b_program = {0};
static inline void ws2818b_program_init(PIO pio, uint sm, uint offset, uint pin, float freq) {
    (void)pio; (void)sm; (void)offset; (void)pin; (void)freq;
}
#endif
