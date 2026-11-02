#include <assert.h>
#include <stdio.h>
#include "matrix.h"

static uint32_t words[LED_COUNT];
static unsigned int word_count;
PIO pio0 = (PIO)1, pio1 = (PIO)2;
bool pio_can_add_program(PIO pio, const struct pio_program *p) { (void)pio; (void)p; return true; }
int pio_claim_unused_sm(PIO pio, bool required) { (void)pio; (void)required; return 0; }
uint pio_add_program(PIO pio, const struct pio_program *p) { (void)pio; (void)p; return 0; }
void pio_sm_put_blocking(PIO pio, uint sm, uint32_t data) {
    (void)pio; (void)sm;
    assert(word_count < LED_COUNT);
    words[word_count++] = data;
}
bool pio_sm_is_tx_fifo_empty(PIO pio, uint sm) { (void)pio; (void)sm; return true; }
void sleep_us(uint64_t us) { (void)us; }
void tight_loop_contents(void) {}

int main(void) {
    const unsigned int visual_to_wire[5][5] = {
        {24, 23, 22, 21, 20}, {15, 16, 17, 18, 19}, {14, 13, 12, 11, 10},
        {5, 6, 7, 8, 9}, {4, 3, 2, 1, 0}
    };
    assert(npInit(LED_PIN));
    for (unsigned int y = 0; y < 5; ++y) {
        for (unsigned int x = 0; x < 5; ++x) {
            npClear();
            npSetPixel(x, y, 1, 2, 3);
            word_count = 0;
            npWrite();
            assert(word_count == LED_COUNT);
            for (unsigned int i = 0; i < LED_COUNT; ++i)
                assert(words[i] == (i == visual_to_wire[y][x] ? 0x02010300u : 0));
        }
    }
    npClear(); npSetPixel(5, 0, 1, 1, 1); npSetPixel(0, 5, 1, 1, 1);
    word_count = 0; npWrite();
    for (unsigned int i = 0; i < LED_COUNT; ++i) assert(words[i] == 0);
    puts("PASS matriz: coordenadas, serpentina, GRB e limites");
}
