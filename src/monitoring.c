#include <stdio.h>
#include "monitoring.h"
#include "session.h"
#include "pico/stdlib.h"
#include "display.h"
#include "mpu6050_i2c.h"

// Após reset, o giroscópio usa a faixa de ±250 graus/s.
#define GYRO_SENS_250DPS 131.0f

typedef struct {
    uint8_t address;
    bool ready;
    bool valid;
    uint32_t timestamp_ms;
    int16_t accel[3];
    int16_t gyro[3];
} sensor_t;

static sensor_t sensors[2] = {
    {.address = MPU6050_ADDR_LOW},
    {.address = MPU6050_ADDR_HIGH}
};
static uint32_t next_sample_ms, started_ms;
static uint32_t paused_at_ms, paused_ms;
static bool paused;
static bool active, serial_connected, result_available;
static monitoring_result_t result;

static bool setup_sensor(sensor_t *sensor) {
    sensor->valid = false;
    sensor->ready = mpu6050_reset(sensor->address) &&
                    mpu6050_set_accel_range(0, sensor->address) &&
                    mpu6050_read_raw(sensor->accel, sensor->gyro, sensor->address);
    return sensor->ready;
}

static void print_sensor(const sensor_t *sensor, unsigned int number) {
    if (!sensor->valid) {
        // Campos vazios: nunca publicar valores antigos como uma nova leitura.
        printf("%lu,%u,ERRO,,,,,,\n", (unsigned long)sensor->timestamp_ms, number);
        return;
    }
    printf("%lu,%u,OK,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f\n",
           (unsigned long)sensor->timestamp_ms, number,
           sensor->accel[0] / ACCEL_SENS_2G,
           sensor->accel[1] / ACCEL_SENS_2G,
           sensor->accel[2] / ACCEL_SENS_2G,
           sensor->gyro[0] / GYRO_SENS_250DPS,
           sensor->gyro[1] / GYRO_SENS_250DPS,
           sensor->gyro[2] / GYRO_SENS_250DPS);
}

static void update_display(const sensor_t sensors[2]) {
    // 15 caracteres por linha com a margem usada pelo módulo display.
    char text[8][16] = {0};
    const char *lines[8];
    for (int i = 0; i < 8; ++i) lines[i] = text[i];

    for (int i = 0; i < 2; ++i) {
        const sensor_t *sensor = &sensors[i];
        int row = i * 4;
        if (!sensor->valid) {
            snprintf(text[row], sizeof(text[row]), "MPU%d ERRO", i + 1);
            snprintf(text[row + 1], sizeof(text[row + 1]), "SEM LEITURA");
            continue;
        }
        snprintf(text[row], sizeof(text[row]), "MPU%d MG DPS", i + 1);
        for (int axis = 0; axis < 3; ++axis) {
            // mg = milésimo de g; dps = graus por segundo.
            int accel_mg = (int)((int32_t)sensor->accel[axis] * 1000 / 16384);
            snprintf(text[row + axis + 1], sizeof(text[0]), "%c%5d %6.1f",
                     "XYZ"[axis], accel_mg, sensor->gyro[axis] / GYRO_SENS_250DPS);
        }
    }
    display_show_lines(lines, 8);
}

static void print_result(void) {
    printf("# SESSION id=%016llx module=MPU\n", (unsigned long long)result.session_id);
    printf("# RESULTADO motivo=%u duracao_ms=%lu MPU1_validas=%lu MPU1_erros=%lu "
           "MPU2_validas=%lu MPU2_erros=%lu\n", (unsigned int)result.reason,
           (unsigned long)result.duration_ms, (unsigned long)result.valid[0],
           (unsigned long)result.errors[0], (unsigned long)result.valid[1],
           (unsigned long)result.errors[1]);
}

void monitoring_init(void) {
    mpu6050_setup_i2c();
    active = serial_connected = result_available = false;
    result = (monitoring_result_t){0};
}

bool monitoring_prepare(void) {
    bool first = setup_sensor(&sensors[0]);
    bool second = setup_sensor(&sensors[1]);
    return first && second;
}

bool monitoring_sensor_ready(unsigned int index) {
    return index < 2 && sensors[index].ready;
}

void monitoring_serial_update(void) {
    bool connected = stdio_usb_connected();
    if (connected && !serial_connected) {
        printf("# Bem-vindo ao Tremo!\n");
        printf("tempo_ms,mpu,status,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps\n");
        if (result_available && !active) print_result();
    }
    serial_connected = connected;
}

void monitoring_start(uint32_t now_ms) {
    result = (monitoring_result_t){0};
    result_available = false;
    result.session_id = session_current()->id;
    paused = false; paused_ms = 0;
    started_ms = next_sample_ms = now_ms;
    active = true;
    for (int i = 0; i < 2; ++i) sensors[i].valid = false;
    if (stdio_usb_connected()) printf("# INICIO SESSAO\n");
}

bool monitoring_update(uint32_t now_ms) {
    if (!active || paused || (int32_t)(now_ms - next_sample_ms) < 0) return true;
    next_sample_ms = now_ms + session_current()->config.sample_ms;
    bool success = true;
    for (int i = 0; i < 2; ++i) {
        sensor_t *sensor = &sensors[i];
        sensor->valid = mpu6050_read_raw(sensor->accel, sensor->gyro, sensor->address);
        sensor->ready = sensor->valid;
        sensor->timestamp_ms = to_ms_since_boot(get_absolute_time());
        if (sensor->valid) ++result.valid[i];
        else { ++result.errors[i]; success = false; }
        if (stdio_usb_connected()) print_sensor(sensor, i + 1);
    }
    return success;
}

void monitoring_draw(void) {
    update_display(sensors);
}

void monitoring_stop(uint32_t now_ms, monitoring_end_t reason) {
    if (!active) return;
    if (paused) monitoring_resume(now_ms);
    active = false;
    result.duration_ms = now_ms - started_ms - paused_ms;
    result.reason = reason;
    result_available = true;
    if (stdio_usb_connected()) print_result();
}

const monitoring_result_t *monitoring_result(void) {
    return &result;
}

void monitoring_pause(uint32_t now_ms) { if (active && !paused) { paused = true; paused_at_ms = now_ms; } }
void monitoring_resume(uint32_t now_ms) {
    if (active && paused) { paused_ms += now_ms - paused_at_ms; paused = false; next_sample_ms = now_ms; }
}
