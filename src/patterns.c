#include "patterns.h"
#include "matrix.h"
#include "pico/rand.h"

// Bit 0 = canto superior esquerdo; três bits por linha. Mesmo brilho e cor.
static const uint16_t masks[PATTERN_COUNT] = {
    0x02f, 0x057, 0x08f, 0x0a7, 0x0cb, 0x0e3, 0x11b, 0x12d,
    0x153, 0x165, 0x18b, 0x195, 0x1a3, 0x1c5, 0x13a, 0x0f1
};

void patterns_clear(void) { npClear(); npWrite(); }

void patterns_draw(uint8_t id) {
    npClear();
    if (id < PATTERN_COUNT) {
        for (uint8_t bit = 0; bit < 9; ++bit)
            if (masks[id] & (1u << bit)) npSetPixel(1 + bit % 3, 1 + bit / 3, 0, 20, 0);
    }
}

void patterns_show(uint8_t id) { patterns_draw(id); npWrite(); }

void patterns_shuffle(uint8_t *items, uint8_t count) {
    for (uint8_t i = count; i > 1; --i) {
        uint8_t j = get_rand_32() % i;
        uint8_t tmp = items[i - 1];
        items[i - 1] = items[j];
        items[j] = tmp;
    }
}
