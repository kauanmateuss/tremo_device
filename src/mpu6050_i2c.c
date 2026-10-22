#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#define MPU6050_I2C_PORT i2c0
#define MPU6050_I2C_SDA 0
#define MPU6050_I2C_SCL 1

void mpu6050_setup_i2c() {
    i2c_init(MPU6050_I2C_PORT, 400 * 1000);
    gpio_set_function(MPU6050_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(MPU6050_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(MPU6050_I2C_SDA);
    gpio_pull_up(MPU6050_I2C_SCL);
}

void mpu6050_reset(char MPU6050_ADDR) {
    uint8_t buf[] = {0x6B, 0x80};
    i2c_write_blocking(MPU6050_I2C_PORT, MPU6050_ADDR, buf, 2, false);
    sleep_ms(100);
    buf[1] = 0x00;
    i2c_write_blocking(MPU6050_I2C_PORT, MPU6050_ADDR, buf, 2, false);
    sleep_ms(10);
}

uint8_t mpu6050_get_accel_range(char MPU6050_ADDR) {
    uint8_t reg = 0x1C;
    uint8_t val;
    i2c_write_blocking(MPU6050_I2C_PORT, MPU6050_ADDR, &reg, 1, true);
    i2c_read_blocking(MPU6050_I2C_PORT, MPU6050_ADDR, &val, 1, false);
    return (val >> 3) & 0x03; // bits 4:3
}

// 0=±2g, 1=±4g, 2=±8g, 3=±16g
void mpu6050_set_accel_range(uint8_t range, char MPU6050_ADDR) {
    uint8_t buf[2];
    buf[0] = 0x1C; // ACCEL_CONFIG register
    buf[1] = range << 3; // bits 3 e 4
    i2c_write_blocking(MPU6050_I2C_PORT, MPU6050_ADDR, buf, 2, false);
}

void mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], char MPU6050_ADDR) {
    uint8_t buffer[6];
    uint8_t reg = 0x3B; //MPU6050_REG_ACCEL_XOUT_H
    i2c_write_blocking(MPU6050_I2C_PORT, MPU6050_ADDR, &reg, 1, true);
    i2c_read_blocking(MPU6050_I2C_PORT, MPU6050_ADDR, buffer, 6, false);
    for (int i=0; i<3; i++)
        accel[i] = (buffer[2*i]<<8) | buffer[2*i+1];

    reg = 0x43; //MPU6050_REG_GYRO_XOUT_H
    i2c_write_blocking(MPU6050_I2C_PORT, MPU6050_ADDR, &reg, 1, true);
    i2c_read_blocking(MPU6050_I2C_PORT, MPU6050_ADDR, buffer, 6, false);
    for (int i=0; i<3; i++)
        gyro[i] = (buffer[2*i]<<8) | buffer[2*i+1];
}