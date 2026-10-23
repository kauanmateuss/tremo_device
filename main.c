#include <stdio.h>
#include "pico/stdlib.h"
#include "display.h"
#include "mpu6050_i2c.h"

#define SAMPLE_INTERVAL_MS 40
#define DISPLAY_INTERVAL_MS 200
#define RETRY_INTERVAL_MS 2000
// Após reset, o giroscópio usa a faixa de ±250 graus/s.
#define GYRO_SENS_250DPS 131.0f

typedef struct {
    uint8_t address;
    bool ready;
    bool valid;
    uint32_t timestamp_ms;
    uint32_t retry_ms;
    int16_t accel[3];
    int16_t gyro[3];
} sensor_t;

static bool setup_sensor(sensor_t *sensor) {
    sensor->valid = false;
    sensor->ready = mpu6050_reset(sensor->address) &&
                    mpu6050_set_accel_range(0, sensor->address);
    sensor->retry_ms = to_ms_since_boot(get_absolute_time()) + RETRY_INTERVAL_MS;
    return sensor->ready;
}

static void read_sensor(sensor_t *sensor) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!sensor->ready && (int32_t)(now - sensor->retry_ms) >= 0)
        setup_sensor(sensor);

    sensor->valid = sensor->ready &&
        mpu6050_read_raw(sensor->accel, sensor->gyro, sensor->address);
    sensor->timestamp_ms = to_ms_since_boot(get_absolute_time());
    if (sensor->ready && !sensor->valid) {
        sensor->ready = false;
        sensor->retry_ms = sensor->timestamp_ms + RETRY_INTERVAL_MS;
    }
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

int main(void) {
    stdio_init_all();
    display_init();
    const char *startup[] = {"INICIANDO MPUS"};
    display_show_lines(startup, 1);
    mpu6050_setup_i2c();

    sensor_t sensors[2] = {
        {.address = MPU6050_ADDR_LOW},
        {.address = MPU6050_ADDR_HIGH}
    };
    for (int i = 0; i < 2; ++i) setup_sensor(&sensors[i]);

    uint32_t next_display_ms = 0;
    bool serial_connected = false;
    while (true) {
        absolute_time_t next_sample = make_timeout_time_ms(SAMPLE_INTERVAL_MS);
        for (int i = 0; i < 2; ++i) read_sensor(&sensors[i]);

        // O monitor USB pode ser aberto depois da inicialização da placa.
        bool connected = stdio_usb_connected();
        if (connected) {
            if (!serial_connected)
                printf("tempo_ms,mpu,status,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps\n");
            for (int i = 0; i < 2; ++i) print_sensor(&sensors[i], i + 1);
        }
        serial_connected = connected;

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if ((int32_t)(now - next_display_ms) >= 0) {
            update_display(sensors);
            next_display_ms = now + DISPLAY_INTERVAL_MS;
        }
        // Se uma operação demorar demais, não tenta recuperar amostras em rajada.
        sleep_until(next_sample);
    }
}
