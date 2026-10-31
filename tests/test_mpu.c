#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mpu6050_i2c.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
static uint8_t regs[128], pointer;
static bool fail;
i2c_inst_t instance, *i2c0 = &instance;
unsigned i2c_init(i2c_inst_t *i, unsigned b) { (void)i; return b; }
void gpio_set_function(uint pin, uint func) { (void)pin; (void)func; }
void gpio_pull_up(uint pin) { (void)pin; }
void sleep_ms(uint32_t ms) { (void)ms; }
int i2c_write_timeout_us(i2c_inst_t *i, uint8_t a, const uint8_t *d, size_t n, bool stop, unsigned timeout) {
    (void)i; (void)a; (void)stop; assert(timeout == 10000);
    if (fail) return -1;
    pointer = d[0]; if (n == 2) regs[pointer] = d[1]; return n;
}
int i2c_read_timeout_us(i2c_inst_t *i, uint8_t a, uint8_t *d, size_t n, bool stop, unsigned timeout) {
    (void)i; (void)a; (void)stop; (void)timeout;
    if (fail) return -1;
    memcpy(d, regs + pointer, n); return n;
}
int main(void) {
    regs[0x75] = 0x68;
    assert(mpu6050_configure(0x69, 20));
    assert(regs[0x19] == 19 && regs[0x1a] == 4 && regs[0x1b] == 0 && regs[0x1c] == 0 && regs[0x6b] == 1);
    assert(mpu6050_configure(0x68, 40)); assert(regs[0x19] == 39 && regs[0x1a] == 5);
    assert(!mpu6050_configure(0x67, 20) && !mpu6050_configure(0x68, 0));
    regs[0x75] = 0; assert(!mpu6050_configure(0x68, 20));
    fail = true; int16_t a[3] = {123}, g[3] = {456};
    assert(!mpu6050_read_raw(a, g, 0x68) && a[0] == 123 && g[0] == 456);
    assert(!mpu6050_configure(0x68, 20));
    puts("PASS MPU: identidade, configuracao, taxas, faixas e falha I2C");
}
