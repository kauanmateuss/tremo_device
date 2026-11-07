#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "monitoring.h"
#include "session.h"
#include "telemetry.h"
#include "pico/stdlib.h"
#include "display.h"
#include "mpu6050_i2c.h"
#ifdef TREMO_FREERTOS_ENABLED
#include "FreeRTOS.h"
#include "semphr.h"
static SemaphoreHandle_t state_mutex;
static void lock_state(void) {
    BaseType_t taken = xSemaphoreTake(state_mutex, portMAX_DELAY);
    configASSERT(taken == pdTRUE);
}
static void unlock_state(void) { xSemaphoreGive(state_mutex); }
#else
static void lock_state(void) {}
static void unlock_state(void) {}
#endif

// Após reset, o giroscópio usa a faixa de ±250 graus/s.
#define GYRO_SENS_250DPS 131.0f

typedef struct {
    uint8_t address;
    bool ready;
    bool valid;
    char setup_error[MPU6050_DIAG_SIZE];
    uint32_t timestamp_ms;
    int16_t accel[3];
    int16_t gyro[3];
    int16_t bias[3];
} sensor_t;

static sensor_t sensors[2] = {
    {.address = MPU6050_ADDR_LOW},
    {.address = MPU6050_ADDR_HIGH}
};
static uint32_t next_sample_ms, started_ms, last_progress_ms;
static uint32_t paused_at_ms, paused_ms;
static bool paused;
static bool active, serial_connected, result_available, result_pending;
static uint32_t duration_limit_ms;
static monitoring_result_t result;
static monitoring_sample_t queue[MONITORING_BUFFER_SIZE];
static unsigned queue_head, queue_count;
static uint32_t sequence;
static calibration_state_t calibration;
static unsigned calibration_count;
static uint32_t calibration_due_ms;
static int64_t gyro_sum[2][3];
static int16_t minimum[2][6], maximum[2][6];
static bool calibrated;
static uint16_t sample_ms;

bool monitoring_pop(monitoring_sample_t *sample) {
    if (!sample) return false;
    lock_state();
    bool available = queue_count > 0;
    if (available) {
        *sample = queue[queue_head];
        queue_head = (queue_head + 1) % MONITORING_BUFFER_SIZE;
        --queue_count;
    }
    unlock_state();
    return available;
}
static void enqueue(const monitoring_sample_t *sample) {
    if (queue_count == MONITORING_BUFFER_SIZE) { ++result.buffer_drops; return; }
    queue[(queue_head + queue_count) % MONITORING_BUFFER_SIZE] = *sample; ++queue_count;
}

static bool setup_sensor(sensor_t *sensor) {
    sensor->valid = sensor->ready = false;
    sensor->setup_error[0] = '\0';
    if (!mpu6050_reset(sensor->address))
        strcpy(sensor->setup_error, "falha I2C no reset/despertar (reg=0x6B)");
    else if (mpu6050_configure(sensor->address, sample_ms, sensor->setup_error)) {
        sensor->ready = mpu6050_read_raw(sensor->accel, sensor->gyro, sensor->address);
        if (!sensor->ready)
            strcpy(sensor->setup_error, "falha I2C na primeira amostra (reg=0x3B)");
    }
    return sensor->ready;
}

// Mantém o último diagnóstico de preparo disponível para quem abrir o USB após o boot.
static void print_setup_errors(void) {
    for (unsigned i = 0; i < 2; ++i)
        if (sensors[i].setup_error[0])
            printf("# MPU 0x%02X: %s\n", sensors[i].address, sensors[i].setup_error);
}

static void print_sample(const monitoring_sample_t *sample) {
    printf("MPU,%016llx,%lu,%llu,%u,%u,%lu", (unsigned long long)sample->session_id,
        (unsigned long)sample->sequence, (unsigned long long)sample->timestamp_us,
        sample->sensor, sample->valid, (unsigned long)sample->read_us);
    if (sample->valid) printf(",%.4f,%.4f,%.4f,%.2f,%.2f,%.2f\n",
        sample->accel[0] / ACCEL_SENS_2G, sample->accel[1] / ACCEL_SENS_2G, sample->accel[2] / ACCEL_SENS_2G,
        sample->gyro[0] / GYRO_SENS_250DPS, sample->gyro[1] / GYRO_SENS_250DPS, sample->gyro[2] / GYRO_SENS_250DPS);
    else printf(",,,,,,\n");
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
                     "XYZ"[axis], accel_mg, ((int32_t)sensor->gyro[axis] - sensor->bias[axis]) / GYRO_SENS_250DPS);
        }
    }
    display_show_lines(lines, 8);
}

static void print_result(const monitoring_result_t *value) {
    printf("# SESSION id=%016llx module=MPU\n", (unsigned long long)value->session_id);
    printf("# RESULTADO motivo=%u duracao_ms=%lu MPU1_validas=%lu MPU1_erros=%lu "
           "MPU2_validas=%lu MPU2_erros=%lu\n", (unsigned int)value->reason,
           (unsigned long)value->duration_ms, (unsigned long)value->valid[0],
           (unsigned long)value->errors[0], (unsigned long)value->valid[1],
           (unsigned long)value->errors[1]);
    printf("# AQUISICAO period_ms=%u calibrated=%u missed_slots=%lu drops=%lu late_ms=%lu read_us=%lu skew_us=%lu\n",
        value->sample_ms, value->calibrated, (unsigned long)value->missed_slots, (unsigned long)value->buffer_drops,
        (unsigned long)value->max_lateness_ms, (unsigned long)value->max_read_us, (unsigned long)value->max_pair_skew_us);
}

void monitoring_init(void) {
#ifdef TREMO_FREERTOS_ENABLED
    if (!state_mutex) state_mutex = xSemaphoreCreateMutex();
    configASSERT(state_mutex);
#endif
    mpu6050_setup_i2c();
    active = serial_connected = result_available = result_pending = false;
    result = (monitoring_result_t){0};
    queue_head = queue_count = 0; calibrated = false; calibration = CAL_IDLE;
}

bool monitoring_prepare(void) {
    lock_state();
    if (active) { unlock_state(); return false; }
    sample_ms = session_config()->sample_ms;
    if (!sample_ms) sample_ms = MONITORING_SAMPLE_MS;
    calibrated = false;
    for (unsigned i = 0; i < 2; ++i) memset(sensors[i].bias, 0, sizeof(sensors[i].bias));
    bool first = setup_sensor(&sensors[0]);
    bool second = setup_sensor(&sensors[1]);
    unlock_state();
    if (serial_connected) print_setup_errors();
    return first && second;
}

bool monitoring_sensor_ready(unsigned int index) {
    lock_state();
    bool ready = index < 2 && sensors[index].ready;
    unlock_state();
    return ready;
}

static void transmit_batch(void) {
    monitoring_sample_t samples[2];
    unsigned n = 0;
    while (n < 2 && monitoring_pop(&samples[n])) ++n;
    if (!n) return;
    char rows[2][256] = {{0}};
    for (unsigned i = 0; i < n; ++i) {
        const monitoring_sample_t *s = &samples[i];
        snprintf(rows[i], sizeof(rows[i]), "[%lu,%llu,%u,%u,%lu,%d,%d,%d,%d,%d,%d,%ld,%ld,%ld]",
            (unsigned long)s->sequence, (unsigned long long)s->timestamp_us, s->sensor, s->valid,
            (unsigned long)s->read_us, s->accel[0], s->accel[1], s->accel[2], s->gyro_raw[0], s->gyro_raw[1], s->gyro_raw[2],
            (long)s->gyro[0], (long)s->gyro[1], (long)s->gyro[2]);
    }
    telemetry_emit(samples[0].session_id, samples[0].timestamp_us, "samples", false,
        "{\"module\":\"mpu\",\"rows\":[%s%s%s]}", rows[0], n == 2 ? "," : "", n == 2 ? rows[1] : "");
}

void monitoring_serial_update(void) {
    lock_state();
    monitoring_result_t snapshot = result;
    bool capturing = active, waiting = paused, available = result_available;
    unlock_state();
    bool connected = stdio_usb_connected();
    if (connected && !serial_connected) {
        printf("# Bem-vindo ao Tremo!\n");
        print_setup_errors();
        printf("mpu,session_id,sequence,timestamp_us,sensor,valid,read_us,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps\n");
        if (available && !capturing) print_result(&snapshot);
    }
    serial_connected = connected;
    if (capturing && !waiting && connected && telemetry_enabled()) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_progress_ms >= 1000) {
            last_progress_ms = now;
            printf("# COLETA ms=%lu MPU1=%lu MPU2=%lu fila_mqtt=%u descartes_tx=%lu\n",
                   (unsigned long)monitoring_elapsed_ms(now), (unsigned long)snapshot.valid[0],
                   (unsigned long)snapshot.valid[1], telemetry_pending(), (unsigned long)telemetry_dropped());
        }
    }
    // Orçamento limitado; nenhuma formatação/USB dentro da leitura I2C.
    if (telemetry_enabled()) transmit_batch();
    else if (connected) {
        monitoring_sample_t sample;
        for (unsigned i = 0; i < 2 && monitoring_pop(&sample); ++i) print_sample(&sample);
    }
}

void monitoring_start(uint32_t now_ms) {
    lock_state();
    duration_limit_ms = session_current()->config.duration_ms;
    result_pending = false;
    result = (monitoring_result_t){0};
    result_available = false;
    result.session_id = session_current()->id;
    result.sample_ms = sample_ms; result.calibrated = calibrated;
    for (unsigned i = 0; i < 2; ++i) memcpy(result.gyro_bias[i], sensors[i].bias, sizeof(sensors[i].bias));
    queue_head = queue_count = 0; sequence = 0;
    paused = false; paused_ms = 0;
    last_progress_ms = started_ms = next_sample_ms = now_ms;
    active = true;
    for (int i = 0; i < 2; ++i) sensors[i].valid = false;
    unlock_state();
    if (stdio_usb_connected()) printf("# INICIO SESSAO\n");
}

static bool acquire(uint32_t now_ms) {
    if (!active || paused || (int32_t)(now_ms - next_sample_ms) < 0) return true;
    uint32_t lateness = now_ms - next_sample_ms;
    if (lateness > result.max_lateness_ms) result.max_lateness_ms = lateness;
    uint32_t skipped = lateness / sample_ms;
    result.missed_slots += skipped;
    sequence += skipped * 2;
    next_sample_ms += (skipped + 1) * sample_ms;
    uint64_t first_us = 0;
    bool success = true;
    for (int i = 0; i < 2; ++i) {
        sensor_t *sensor = &sensors[i];
        monitoring_sample_t sample = {.session_id = result.session_id, .sequence = sequence++, .sensor = i + 1};
        sample.timestamp_us = time_us_64();
        if (i == 0) first_us = sample.timestamp_us;
        else if (sample.timestamp_us - first_us > result.max_pair_skew_us) result.max_pair_skew_us = sample.timestamp_us - first_us;
        sensor->valid = mpu6050_read_raw(sensor->accel, sensor->gyro, sensor->address);
        sensor->ready = sensor->valid;
        sensor->timestamp_ms = to_ms_since_boot(get_absolute_time());
        if (sensor->valid) ++result.valid[i];
        else { ++result.errors[i]; success = false; }
        sample.valid = sensor->valid;
        sample.read_us = time_us_64() - sample.timestamp_us;
        if (sample.read_us > result.max_read_us) result.max_read_us = sample.read_us;
        if (sample.valid) for (unsigned axis = 0; axis < 3; ++axis) {
            sample.accel[axis] = sensor->accel[axis]; sample.gyro_raw[axis] = sensor->gyro[axis];
            sample.gyro[axis] = (int32_t)sensor->gyro[axis] - sensor->bias[axis];
        }
        enqueue(&sample);
    }
    return success;
}

void monitoring_draw(void) {
    sensor_t snapshot[2];
    lock_state();
    memcpy(snapshot, sensors, sizeof(snapshot));
    unlock_state();
    update_display(snapshot);
}

// Chamado com o mutex retido. Não faz I/O nem formatação.
static void stop_capture(uint32_t now_ms, monitoring_end_t reason) {
    if (paused) {
        paused_ms += now_ms - paused_at_ms;
        paused = false;
    }
    active = false;
    result.duration_ms = now_ms - started_ms - paused_ms;
    result.reason = reason;
    result_available = result_pending = true;
}

void monitoring_worker_update(void) {
    lock_state();
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (active && !paused) {
        if (now - started_ms - paused_ms >= duration_limit_ms)
            stop_capture(now, MONITORING_COMPLETE);
        else if (!acquire(now))
            stop_capture(to_ms_since_boot(get_absolute_time()), MONITORING_SENSOR_ERROR);
    }
    unlock_state();
}

bool monitoring_update(uint32_t now_ms) {
    lock_state();
#ifdef TREMO_FREERTOS_ENABLED
    (void)now_ms;
    bool healthy = !result_available || result.reason != MONITORING_SENSOR_ERROR;
#else
    bool healthy = acquire(now_ms);
#endif
    unlock_state();
    return healthy;
}

void monitoring_stop(uint32_t now_ms, monitoring_end_t reason) {
    lock_state();
    if (active) stop_capture(now_ms, reason);
    bool publish = result_pending;
    result_pending = false;
    unlock_state();
    if (!publish) return;
    if (telemetry_enabled()) {
        while (queue_count) transmit_batch();
        telemetry_emit(result.session_id, time_us_64(), "result", true,
            "{\"module\":\"mpu\",\"reason\":%u,\"duration_ms\":%lu,\"sample_ms\":%u,\"calibrated\":%s,\"valid\":[%lu,%lu],\"errors\":[%lu,%lu],\"missed_slots\":%lu,\"buffer_drops\":%lu,\"max_lateness_ms\":%lu,\"max_read_us\":%lu,\"max_pair_skew_us\":%lu,\"gyro_bias\":[[%d,%d,%d],[%d,%d,%d]]}",
            result.reason, (unsigned long)result.duration_ms, result.sample_ms, result.calibrated ? "true" : "false",
            (unsigned long)result.valid[0], (unsigned long)result.valid[1], (unsigned long)result.errors[0], (unsigned long)result.errors[1],
            (unsigned long)result.missed_slots, (unsigned long)result.buffer_drops, (unsigned long)result.max_lateness_ms,
            (unsigned long)result.max_read_us, (unsigned long)result.max_pair_skew_us,
            result.gyro_bias[0][0], result.gyro_bias[0][1], result.gyro_bias[0][2], result.gyro_bias[1][0], result.gyro_bias[1][1], result.gyro_bias[1][2]);
    }
    if (stdio_usb_connected()) print_result(&result);
}

const monitoring_result_t *monitoring_result(void) {
    static monitoring_result_t snapshot;
    lock_state();
    snapshot = result;
    unlock_state();
    return &snapshot;
}

void monitoring_pause(uint32_t now_ms) {
    lock_state();
    if (active && !paused) { paused = true; paused_at_ms = now_ms; }
    unlock_state();
}
void monitoring_resume(uint32_t now_ms) {
    lock_state();
    if (active && paused) { paused_ms += now_ms - paused_at_ms; paused = false; next_sample_ms = now_ms; }
    unlock_state();
}

void monitoring_calibration_begin(uint32_t now_ms) {
    lock_state();
    calibrated = false; calibration = CAL_RUNNING; calibration_count = 0;
    calibration_due_ms = now_ms + 300; // Estabilização do filtro após configurar.
    memset(gyro_sum, 0, sizeof(gyro_sum));
    for (unsigned i = 0; i < 2; ++i) {
        memset(sensors[i].bias, 0, sizeof(sensors[i].bias));
        for (unsigned a = 0; a < 6; ++a) { minimum[i][a] = INT16_MAX; maximum[i][a] = INT16_MIN; }
    }
    unlock_state();
}
static calibration_state_t calibrate(uint32_t now_ms) {
    if (calibration != CAL_RUNNING || (int32_t)(now_ms - calibration_due_ms) < 0) return calibration;
    calibration_due_ms = now_ms + sample_ms;
    for (unsigned i = 0; i < 2; ++i) {
        int16_t a[3], g[3];
        if (!mpu6050_read_raw(a, g, sensors[i].address)) { sensors[i].ready = false; return calibration = CAL_SENSOR_ERROR; }
        int64_t norm = 0;
        for (unsigned axis = 0; axis < 3; ++axis) {
            norm += (int64_t)a[axis] * a[axis];
            if (abs(g[axis]) > 1965) return calibration = CAL_MOVING; // 15 graus/s.
            gyro_sum[i][axis] += g[axis];
        }
        if (norm < 171798692ll || norm > 386547057ll) return calibration = CAL_MOVING; // 0,8..1,2 g.
        for (unsigned axis = 0; axis < 6; ++axis) {
            int16_t value = axis < 3 ? a[axis] : g[axis - 3];
            if (value < minimum[i][axis]) minimum[i][axis] = value;
            if (value > maximum[i][axis]) maximum[i][axis] = value;
            if ((int32_t)maximum[i][axis] - minimum[i][axis] > (axis < 3 ? 1500 : 262)) return calibration = CAL_MOVING;
        }
    }
    if (++calibration_count == MONITORING_CAL_SAMPLES) {
        for (unsigned i = 0; i < 2; ++i) for (unsigned axis = 0; axis < 3; ++axis)
            sensors[i].bias[axis] = gyro_sum[i][axis] / MONITORING_CAL_SAMPLES;
        calibrated = true; calibration = CAL_OK;
    }
    return calibration;
}
calibration_state_t monitoring_calibration_update(uint32_t now_ms) {
    lock_state();
    calibration_state_t value = calibrate(now_ms);
    unlock_state();
    return value;
}
void monitoring_calibration_cancel(void) {
    lock_state();
    calibration = CAL_IDLE;
    calibrated = false;
    unlock_state();
}

uint32_t monitoring_elapsed_ms(uint32_t now_ms) {
    lock_state();
    uint32_t elapsed = active ? (paused ? paused_at_ms : now_ms) - started_ms - paused_ms : result.duration_ms;
    unlock_state();
    return elapsed;
}
