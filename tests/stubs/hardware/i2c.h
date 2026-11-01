#ifndef TEST_I2C_H
#define TEST_I2C_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct { int unused; } i2c_inst_t;
extern i2c_inst_t *i2c0, *i2c1;
int i2c_write_blocking_until(i2c_inst_t *i2c, uint8_t addr, const uint8_t *data, size_t len, bool nostop, uint64_t until);
unsigned i2c_init(i2c_inst_t *i2c, unsigned baud);
int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *data, size_t len, bool nostop, unsigned timeout);
int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *data, size_t len, bool nostop, unsigned timeout);
#endif
