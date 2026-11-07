#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "mpu_task.h"
#include "monitoring.h"
#include "mpu6050_i2c.h"
#include "session.h"
#include "telemetry.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

static session_t session = {.id = 42, .config = {.sample_ms = 20, .duration_ms = 500}};
static bool failed, transmit;
static TaskHandle_t consumer_handle;
uint64_t time_us_64(void) { return (uint64_t)xTaskGetTickCount() * 1000; }
absolute_time_t get_absolute_time(void) { return time_us_64(); }
uint32_t to_ms_since_boot(absolute_time_t value) { return value / 1000; }
bool stdio_usb_connected(void) { return false; }
const session_t *session_current(void) { return &session; }
const session_config_t *session_config(void) { return &session.config; }
void mpu6050_setup_i2c(void) {}
bool mpu6050_reset(uint8_t address) { (void)address; return true; }
bool mpu6050_configure(uint8_t address, uint16_t period, char *error) {
    (void)address; (void)error; assert(period == 20); return true;
}
bool mpu6050_read_raw(int16_t a[3], int16_t g[3], uint8_t address) {
    // O preparo é feito pelo consumidor, a coleta pela TaskMPU.
    vTaskDelay(1);
    a[0] = a[1] = g[0] = g[1] = g[2] = 0; a[2] = 16384;
    return !failed || address != 0x69;
}
void display_show_lines(const char *lines[], size_t count) {
    (void)lines; (void)count;
    assert(xTaskGetCurrentTaskHandle() == consumer_handle);
    vTaskDelay(100);
}
bool telemetry_enabled(void) { return transmit; }
unsigned telemetry_pending(void) { return 0; }
uint32_t telemetry_dropped(void) { return 0; }
void telemetry_emit(uint64_t id, uint64_t timestamp, const char *type,
                    bool essential, const char *format, ...) {
    (void)timestamp; (void)type; (void)essential; (void)format;
    assert(id == 42 && xTaskGetCurrentTaskHandle() == consumer_handle);
    vTaskDelay(100);
}
void rtos_assert_failed(const char *file, int line) {
    fprintf(stderr, "RTOS assert %s:%d\n", file, line); abort();
}
void vApplicationMallocFailedHook(void) { abort(); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task; (void)name; abort();
}

static uint32_t now_ms(void) { return time_us_64() / 1000; }
static void consumer(void *parameter) {
    (void)parameter;
    monitoring_init();
    assert(monitoring_prepare());
    mpu_task_start();
    monitoring_start(now_ms());
    vTaskDelay(60);
    uint32_t before = monitoring_result()->valid[0];
    monitoring_draw();
    assert(monitoring_result()->valid[0] >= before + 3);
    before = monitoring_result()->valid[0];
    transmit = true;
    monitoring_serial_update();
    transmit = false;
    assert(monitoring_result()->valid[0] >= before + 3);

    monitoring_pause(now_ms());
    before = monitoring_result()->valid[0];
    vTaskDelay(80);
    assert(monitoring_result()->valid[0] == before);
    monitoring_resume(now_ms());
    vTaskDelay(40);
    assert(monitoring_result()->valid[0] > before);
    monitoring_stop(now_ms(), MONITORING_INTERRUPTED);
    monitoring_result_t result = *monitoring_result();
    assert(result.reason == MONITORING_INTERRUPTED && result.duration_ms < 400);
    monitoring_stop(now_ms(), MONITORING_COMPLETE);
    assert(monitoring_result()->reason == MONITORING_INTERRUPTED);

    session.config.duration_ms = 100;
    monitoring_start(now_ms());
    vTaskDelay(180); // Termina mesmo sem atualizações da TaskSession.
    result = *monitoring_result();
    assert(result.reason == MONITORING_COMPLETE && result.duration_ms >= 100);
    assert(result.duration_ms < 120 && result.valid[0] >= 4);
    vTaskDelay(40);
    assert(monitoring_result()->valid[0] == result.valid[0]);
    monitoring_stop(now_ms(), MONITORING_INTERRUPTED);
    assert(monitoring_result()->reason == MONITORING_COMPLETE);
    monitoring_sample_t sample;
    uint32_t sequence = 0;
    uint64_t previous = 0;
    while (monitoring_pop(&sample)) {
        assert(sample.session_id == 42 && sample.sequence == sequence++);
        assert(sample.timestamp_us >= previous && sample.read_us >= 1000);
        previous = sample.timestamp_us;
    }

    session.config.duration_ms = 500;
    monitoring_start(now_ms());
    failed = true;
    vTaskDelay(40);
    assert(!monitoring_update(now_ms()));
    assert(monitoring_result()->reason == MONITORING_SENSOR_ERROR);
    assert(monitoring_result()->errors[1] == 1);
    assert(monitoring_pop(&sample) && sample.valid);
    assert(monitoring_pop(&sample) && !sample.valid && sample.accel[2] == 0);
    monitoring_stop(now_ms(), MONITORING_COMPLETE);
    assert(monitoring_result()->reason == MONITORING_SENSOR_ERROR);

    failed = false;
    session.config.duration_ms = 4000;
    monitoring_start(now_ms());
    vTaskDelay(2800); // Consumidor lento enche o buffer, captura continua.
    assert(monitoring_result()->buffer_drops > 0);
    before = monitoring_result()->valid[0];
    vTaskDelay(40);
    assert(monitoring_result()->valid[0] > before);
    monitoring_stop(now_ms(), MONITORING_INTERRUPTED);
    puts("PASS RTOS MPU: captura independente de OLED/telemetria, pausa, término, falha e perdas");
    exit(0);
}

int main(void) {
    assert(mpu_task_init());
    assert(xTaskCreate(consumer, "session", 2048, NULL, 2, &consumer_handle) == pdPASS);
    vTaskStartScheduler();
    return 1;
}
