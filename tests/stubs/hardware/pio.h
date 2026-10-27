#ifndef TEST_PIO_H
#define TEST_PIO_H
#include "pico/stdlib.h"
typedef void *PIO;
extern PIO pio0, pio1;
struct pio_program { int unused; };
bool pio_can_add_program(PIO pio, const struct pio_program *program);
int pio_claim_unused_sm(PIO pio, bool required);
uint pio_add_program(PIO pio, const struct pio_program *program);
void pio_sm_put_blocking(PIO pio, uint sm, uint32_t data);
bool pio_sm_is_tx_fifo_empty(PIO pio, uint sm);
#endif
