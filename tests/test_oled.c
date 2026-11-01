#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ssd1306.h"

static i2c_inst_t port;
i2c_inst_t *i2c1 = &port;
static uint64_t now;
static unsigned calls, fail_call;
static uint8_t pixels[ssd1306_buffer_length];
absolute_time_t make_timeout_time_us(uint64_t us) { return now + us; }

int i2c_write_blocking_until(i2c_inst_t *p, uint8_t addr, const uint8_t *data,
                             size_t len, bool nostop, uint64_t until) {
    assert(p == i2c1 && addr == 0x3C && !nostop && until >= now);
    ++calls;
    if (calls == fail_call) { now = until; return -1; } // Barramento preso até o prazo.
    if (calls == 1) {
        const uint8_t expected[] = {0, 0x21, 0, 127, 0x22, 0, 7};
        assert(len == sizeof(expected) && memcmp(data, expected, len) == 0);
        now += 1000;
    } else {
        assert(len == sizeof(pixels) + 1 && data[0] == 0x40);
        assert(memcmp(data + 1, pixels, sizeof(pixels)) == 0);
    }
    return (int)len;
}
int i2c_write_timeout_us(i2c_inst_t *p, uint8_t addr, const uint8_t *data,
                         size_t len, bool nostop, unsigned timeout) {
    return i2c_write_blocking_until(p, addr, data, len, nostop, now + timeout);
}

int main(void) {
    struct render_area area = {0,127,0,7, sizeof(pixels)};
    pixels[10] = 0x55;
    for (fail_call = 1; fail_call <= 2; ++fail_call) {
        calls = 0;
        uint64_t before = now;
        assert(!render_on_display(pixels, &area));
        assert(now - before == 40000 && calls == fail_call);
    }
    calls = fail_call = 0; // O quadro seguinte volta a funcionar sem reiniciar o firmware.
    assert(render_on_display(pixels, &area) && calls == 2);
    puts("PASS OLED: falha nos comandos ou pixels respeita o prazo total; proximo quadro recupera");
}
