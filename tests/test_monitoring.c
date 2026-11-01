#include "mpu6050_i2c.h"
#include <assert.h>
#include <stdio.h>
#include "monitoring.h"
#include "session.h"
#include "pico/stdlib.h"
static uint64_t clock_us;
static bool fail, moving, changed;
static unsigned reads;
uint64_t time_us_64(void) { return clock_us; }
absolute_time_t get_absolute_time(void) { return clock_us; }
uint32_t to_ms_since_boot(absolute_time_t t) { return t / 1000; }
bool stdio_usb_connected(void) { return false; }
void display_show_lines(const char *lines[], size_t n) { (void)lines; (void)n; }
void mpu6050_setup_i2c(void) {}
bool mpu6050_reset(uint8_t address) { (void)address; return !fail; }
bool mpu6050_configure(uint8_t address, uint16_t ms, char error[MPU6050_DIAG_SIZE]) { (void)error; (void)address; assert(ms == 20 || ms == 40); return !fail; }
bool mpu6050_read_raw(int16_t a[3], int16_t g[3], uint8_t addr) {
    ++reads; clock_us += 500;
    if (fail) return false;
    a[0] = a[1] = 0; a[2] = 16384;
    g[0] = moving ? 3000 : changed ? 900 : (addr == 0x68 ? 131 : -262);
    g[1] = g[2] = 0; return true;
}
static void at(uint32_t ms) { clock_us = (uint64_t)ms * 1000; }
int main(void) {
    session_init(123); monitoring_init(); assert(monitoring_prepare());
    monitoring_calibration_begin(0);
    at(299); assert(monitoring_calibration_update(299) == CAL_RUNNING);
    for (unsigned i = 0; i < MONITORING_CAL_SAMPLES; ++i) { at(300 + i * 40); monitoring_calibration_update(clock_us / 1000); }
    assert(monitoring_calibration_update(clock_us / 1000) == CAL_OK);
    assert(session_begin(SESSION_TREMOR, clock_us));
    monitoring_start(3000); at(3000); assert(monitoring_update(3000));
    monitoring_sample_t s; assert(monitoring_pop(&s));
    assert(s.valid && s.gyro_raw[0] == 131 && s.gyro[0] == 0 && s.accel[2] == 16384);
    assert(s.timestamp_us == 3000000 && s.read_us == 500 && s.session_id == session_current()->id);
    assert(monitoring_pop(&s) && s.sensor == 2 && s.gyro[0] == 0);
    assert(monitoring_result()->gyro_bias[1][0] == -262 && monitoring_result()->calibrated);
    at(3130); monitoring_update(3130);
    assert(monitoring_result()->missed_slots == 2 && monitoring_result()->max_lateness_ms == 90);
    assert(monitoring_pop(&s) && s.sequence == 6); assert(monitoring_pop(&s) && s.sequence == 7);
    assert(monitoring_result()->max_pair_skew_us == 500 && monitoring_result()->max_read_us == 500);
    monitoring_pause(3140); unsigned before = reads;
    at(8000); monitoring_update(8000); assert(reads == before);
    monitoring_resume(9000); at(9000); monitoring_update(9000); assert(monitoring_result()->missed_slots == 2);
    for (unsigned i = 1; i <= MONITORING_BUFFER_SIZE; ++i) { at(9000 + i * 40); monitoring_update(clock_us / 1000); }
    assert(monitoring_result()->buffer_drops > 0);
    while (monitoring_pop(&s)) {}
    fail = true; at(30000); assert(!monitoring_update(30000));
    assert(monitoring_pop(&s) && !s.valid && !s.accel[2]);
    monitoring_stop(30000, MONITORING_SENSOR_ERROR); assert(monitoring_result()->errors[0] == 1);
    fail = false; assert(monitoring_prepare()); moving = true;
    monitoring_calibration_begin(0); at(300); assert(monitoring_calibration_update(300) == CAL_MOVING);
    moving = false; monitoring_calibration_begin(0); at(300); monitoring_calibration_update(300);
    changed = true; at(340); assert(monitoring_calibration_update(340) == CAL_MOVING);
    changed = false; monitoring_calibration_begin(0); fail = true;
    at(300); assert(monitoring_calibration_update(300) == CAL_SENSOR_ERROR);
    monitoring_calibration_cancel(); assert(monitoring_calibration_update(9999) == CAL_IDLE);
    puts("PASS aquisicao: calibracao, gravidade, atrasos, timestamps, fila, pausa e falhas");
}
