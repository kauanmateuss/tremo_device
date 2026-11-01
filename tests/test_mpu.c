#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mpu6050_i2c.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
static uint8_t regs[128], pointer;
static bool fail;
static uint8_t read_xor[128]; // Simula bits que diferem do valor escrito.
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
    memcpy(d, regs + pointer, n);
    for (size_t j = 0; j < n; ++j) d[j] ^= read_xor[pointer + j];
    return n;
}
int main(void) {
    char error[MPU6050_DIAG_SIZE];
    regs[0x75] = 0x68;
    assert(mpu6050_configure(0x69, 20, error));
    assert(regs[0x19] == 19 && regs[0x1a] == 4 && regs[0x1b] == 0 && regs[0x1c] == 0 && regs[0x6b] == 1);
    assert(mpu6050_configure(0x68, 40, error)); assert(regs[0x19] == 39 && regs[0x1a] == 5);
    assert(!mpu6050_configure(0x67, 20, error) && !mpu6050_configure(0x68, 0, error));
    read_xor[0x75] = 0x81; // Bits 7 e 0 não fazem parte da identidade.
    read_xor[0x1A] = 0xC0;
    read_xor[0x1B] = read_xor[0x1C] = 0x07;
    assert(mpu6050_configure(0x68, 40, error) && mpu6050_configure(0x69, 20, error));
    // Uma taxa, filtro ou escala incorreta ainda precisa impedir a aquisição.
    for (unsigned reg = 0x19; reg <= 0x1C; ++reg) {
        uint8_t bit = reg < 0x1B ? 1 : 8;
        read_xor[reg] ^= bit;
        assert(!mpu6050_configure(0x68, 40, error) && strstr(error, "lido="));
        read_xor[reg] ^= bit;
    }
    memset(read_xor, 0, sizeof(read_xor));
    // Reproduz o log da placa: os dois endereços retornam a identidade 0x70.
    regs[0x75] = 0x70;
    for (uint8_t addr = 0x68; addr <= 0x69; ++addr) {
        for (unsigned ms = 20; ms <= 40; ms += 20) {
            uint8_t filter = ms == 20 ? 4 : 5;
            regs[0x1D] = 0x08; // Começa com o filtro do acelerômetro desabilitado.
            assert(mpu6050_configure(addr, ms, error));
            assert(regs[0x19] == ms - 1 && regs[0x1A] == filter);
            assert(regs[0x1B] == 0 && regs[0x1C] == 0 && regs[0x1D] == filter);
        }
    }
    read_xor[0x1B] = 1; // Bypass do filtro do giroscópio não pode passar na releitura.
    assert(!mpu6050_configure(0x68, 40, error) && strstr(error, "reg=0x1B"));
    read_xor[0x1B] = 0;
    read_xor[0x1D] = 8;
    assert(!mpu6050_configure(0x69, 20, error) && strstr(error, "reg=0x1D"));
    regs[0x75] = 0x68;
    regs[0x1D] = 0xAB; // No 6050 este registrador não configura o filtro.
    assert(mpu6050_configure(0x69, 20, error) && regs[0x1D] == 0xAB);
    regs[0x75] = 0x71; // Não confundir um modelo vizinho com o MPU-6500.
    assert(!mpu6050_configure(0x68, 20, error) && strstr(error, "lido=0x71"));
    regs[0x75] = 0; assert(!mpu6050_configure(0x68, 20, error));
    fail = true; int16_t a[3] = {123}, g[3] = {456};
    assert(!mpu6050_read_raw(a, g, 0x68) && a[0] == 123 && g[0] == 456);
    assert(!mpu6050_configure(0x68, 20, error));
    puts("PASS MPU: identidade, configuracao, taxas, faixas e falha I2C");
}
