#ifndef MPU6050_I2C_H
#define MPU6050_I2C_H

#include <stdbool.h>
#include <stdint.h>

#define MPU6050_ADDR_LOW  0x68
#define MPU6050_ADDR_HIGH 0x69
#define MPU6050_RANGE_ERROR 0xFF
#define ACCEL_SENS_2G  16384.0f
#define ACCEL_SENS_4G  8192.0f
#define ACCEL_SENS_8G  4096.0f
#define ACCEL_SENS_16G 2048.0f

// Inicialize o barramento uma vez antes de acessar os dois sensores.
void mpu6050_setup_i2c(void);
bool mpu6050_reset(uint8_t address);
// WHO_AM_I vale 0x68 também no endereço I2C 0x69.
bool mpu6050_configure(uint8_t address, uint16_t sample_ms);
// Retorna 0=±2g, 1=±4g, 2=±8g, 3=±16g ou MPU6050_RANGE_ERROR.
uint8_t mpu6050_get_accel_range(uint8_t address);
bool mpu6050_set_accel_range(uint8_t range, uint8_t address);
// Retorna false em falha, sem alterar os vetores de saída.
bool mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], uint8_t address);

#endif
