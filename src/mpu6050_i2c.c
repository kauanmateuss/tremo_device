#include "mpu6050_i2c.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define MPU6050_I2C_PORT i2c0
#define MPU6050_I2C_SDA 0
#define MPU6050_I2C_SCL 1
#define MPU6050_TIMEOUT_US 10000
#define REG_PWR_MGMT_1 0x6B
#define REG_ACCEL_CONFIG 0x1C
#define REG_ACCEL_XOUT_H 0x3B

static bool valid_address(uint8_t address) {
    return address == MPU6050_ADDR_LOW || address == MPU6050_ADDR_HIGH;
}

static bool write_register(uint8_t address, uint8_t reg, uint8_t value) {
    uint8_t data[] = {reg, value};
    return valid_address(address) &&
        i2c_write_timeout_us(MPU6050_I2C_PORT, address, data, 2, false,
                             MPU6050_TIMEOUT_US) == 2;
}

static bool read_registers(uint8_t address, uint8_t reg, uint8_t *data, size_t length) {
    if (!valid_address(address)) return false;
    if (i2c_write_timeout_us(MPU6050_I2C_PORT, address, &reg, 1, true,
                             MPU6050_TIMEOUT_US) != 1) return false;
    return i2c_read_timeout_us(MPU6050_I2C_PORT, address, data, length, false,
                              MPU6050_TIMEOUT_US) == (int)length;
}

void mpu6050_setup_i2c(void) {
    i2c_init(MPU6050_I2C_PORT, 400000);
    gpio_set_function(MPU6050_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(MPU6050_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(MPU6050_I2C_SDA);
    gpio_pull_up(MPU6050_I2C_SCL);
}

bool mpu6050_reset(uint8_t address) {
    if (!write_register(address, REG_PWR_MGMT_1, 0x80)) return false;
    sleep_ms(100);
    if (!write_register(address, REG_PWR_MGMT_1, 0x00)) return false;
    sleep_ms(100);
    return true;
}

uint8_t mpu6050_get_accel_range(uint8_t address) {
    uint8_t value;
    if (!read_registers(address, REG_ACCEL_CONFIG, &value, 1))
        return MPU6050_RANGE_ERROR;
    return (value >> 3) & 0x03;
}

bool mpu6050_set_accel_range(uint8_t range, uint8_t address) {
    uint8_t value;
    if (range > 3 || !read_registers(address, REG_ACCEL_CONFIG, &value, 1))
        return false;
    // Preserva os demais bits do registrador.
    value = (value & ~0x18u) | (range << 3);
    return write_register(address, REG_ACCEL_CONFIG, value);
}

bool mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], uint8_t address) {
    uint8_t data[14];
    if (!accel || !gyro || !read_registers(address, REG_ACCEL_XOUT_H, data, sizeof(data)))
        return false;
    // Leitura única: aceleração, temperatura (ignorada) e giroscópio.
    for (int i = 0; i < 3; ++i) {
        accel[i] = (int16_t)(((uint16_t)data[2*i] << 8) | data[2*i+1]);
        gyro[i] = (int16_t)(((uint16_t)data[8+2*i] << 8) | data[9+2*i]);
    }
    return true;
}

// DLPF habilitado: taxa = 1000 / (1 + SMPLRT_DIV). Faixas: 2g e 250 dps.
bool mpu6050_configure(uint8_t address, uint16_t sample_ms) {
    if (sample_ms != 20 && sample_ms != 40) return false;
    uint8_t identity, actual[4];
    uint8_t expected[] = {(uint8_t)(sample_ms - 1), sample_ms == 20 ? 4 : 5, 0, 0};
    if (!read_registers(address, 0x75, &identity, 1) || identity != 0x68) return false;
    if (!write_register(address, REG_PWR_MGMT_1, 0x01)) return false; // PLL X gyro.
    for (unsigned i = 0; i < 4; ++i)
        if (!write_register(address, 0x19 + i, expected[i])) return false;
    if (!read_registers(address, 0x19, actual, sizeof(actual))) return false;
    for (unsigned i = 0; i < 4; ++i) if (actual[i] != expected[i]) return false;
    return true;
}
