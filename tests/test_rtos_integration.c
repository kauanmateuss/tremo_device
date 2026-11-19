#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "session.h"
#include "monitoring.h"
#include "mot.h"
#include "communication.h"
#include "telemetry.h"
#include "input_task.h"
#include "display_task.h"
#include "session_task.h"
#include "mpu_task.h"
#include "mpu6050_i2c.h"
#include "display.h"
#include "matrix.h"
#include "network_fixture.h"
#include "pico/cyw43_arch.h"
#include "FreeRTOS.h"
#include "task.h"

static bool pins[30];
static uint16_t axes[2] = {2048, 2048};
static unsigned adc_channel, sensor_reads, frames;
static uint8_t frame[1024], saved_frame[1024];
static struct render_area area = {.buffer_length = sizeof(frame)};
static npLED_t pixels[LED_COUNT];

unsigned get_core_num(void) { return 0; }
uint32_t get_rand_32(void) { return 1234; }
absolute_time_t get_absolute_time(void) { return time_us_64(); }
uint32_t to_ms_since_boot(absolute_time_t time) { return time / 1000; }
void stdio_init_all(void) {}
bool stdio_usb_connected(void) { return false; }
void gpio_init(uint pin) { (void)pin; }
void gpio_set_dir(uint pin, bool output) { (void)pin; (void)output; }
void gpio_pull_up(uint pin) { (void)pin; }
bool gpio_get(uint pin) { return pins[pin]; }
void adc_init(void) {}
void adc_gpio_init(uint pin) { (void)pin; }
void adc_select_input(uint input) { adc_channel = input; }
uint16_t adc_read(void) { return axes[adc_channel]; }

void display_init(void) {}
void display_save_screen(void) { memcpy(saved_frame, frame, sizeof(frame)); }
void display_restore_screen(void) { memcpy(frame, saved_frame, sizeof(frame)); display_render(); }
void display_render(void) { assert(display_task_render(frame, &area)); }
void display_draw_text(const char *text, uint8_t x, uint8_t y) { (void)text; (void)x; (void)y; }
void display_show_lines(const char *lines[], size_t count) {
    assert(!strcmp(pcTaskGetName(NULL), "TaskSession") && count <= 8);
    for (size_t i = 0; i < count; ++i) assert(strlen(lines[i]) <= 16);
    display_render();
}
bool render_on_display(uint8_t *buffer, struct render_area *value) {
    (void)buffer; (void)value;
    assert(!strcmp(pcTaskGetName(NULL), "TaskDisplay"));
    ++frames; vTaskDelay(25); return true;
}
bool npInit(uint pin) { return pin == LED_PIN; }
void npClear(void) { memset(pixels, 0, sizeof(pixels)); }
void npSetPixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b) {
    assert(x < 5 && y < 5);
    pixels[y * 5 + x] = (npLED_t){.R = r, .G = g, .B = b};
}
void npWrite(void) { (void)display_task_matrix(pixels); }
uint64_t npWriteTimed(void) { return display_task_matrix(pixels); }
uint64_t matrix_write_frame(const npLED_t *value) {
    (void)value;
    assert(!strcmp(pcTaskGetName(NULL), "TaskDisplay"));
    vTaskDelay(1); return time_us_64();
}
void mpu6050_setup_i2c(void) {}
bool mpu6050_reset(uint8_t address) { (void)address; vTaskDelay(5); return true; }
bool mpu6050_configure(uint8_t address, uint16_t period, char *error) {
    (void)address; (void)error; return period == 20 || period == 40;
}
bool mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], uint8_t address) {
    (void)address; ++sensor_reads;
    accel[0] = accel[1] = gyro[0] = gyro[1] = gyro[2] = 0;
    accel[2] = 16384; vTaskDelay(1); return true;
}

static void wait_app(app_state_t state) {
    for (unsigned i = 0; i < 500; ++i) {
        if (app_get_state() == state) return;
        vTaskDelay(10);
    }
    fprintf(stderr, "Estado esperado=%u atual=%u\n", state, app_get_state());
    abort();
}
static void press(uint pin) {
    pins[pin] = false; vTaskDelay(45);
    pins[pin] = true; vTaskDelay(45);
}
static void move(unsigned axis, uint16_t value) {
    axes[axis] = value; vTaskDelay(45);
    axes[axis] = 2048; vTaskDelay(45);
}

static void user_script(void *parameter) {
    (void)parameter;
    wait_app(APP_MENU);
    move(0, 4095); press(5); wait_app(APP_DEVICE_INFO); press(5); wait_app(APP_INFO); // Configurações.
    move(0, 4095); press(5); // MPU COG SIM.
    assert(session_config()->cognitive_mpu);
    press(6); wait_app(APP_MENU);
    move(0, 4095); press(5); wait_app(APP_SESSIONS);
    press(5); wait_app(APP_SESSION_ACTIVE);
    press(22); wait_app(APP_MENU);
    move(0, 4095); press(5); wait_app(APP_COG_MENU);
    press(5); wait_app(APP_READY); // Preparo e calibração pelo fluxo real.
    press(5); wait_app(APP_MOT);
    assert(mot_get_state() == MOT_INSTRUCTIONS);
    unsigned before = sensor_reads;
    vTaskDelay(100);
    assert(sensor_reads > before);
    press(22);
    assert(session_current()->state == SESSION_PAUSED);
    before = sensor_reads; vTaskDelay(100); assert(sensor_reads == before);
    press(5); assert(session_current()->state == SESSION_RUNNING);
    press(5); assert(mot_get_state() == MOT_STIMULUS);
    mock_link = 0;
    before = sensor_reads;
    vTaskDelay(1200);
    assert(sensor_reads > before && mock_joins > 0);
    assert(telemetry_dropped() > 0);
    move(1, 0); press(5); // Primeiro alvo, à esquerda do centro.
    assert(mot_result()->record_count == 1 && mot_result()->records[0].outcome == MOT_CORRECT);
    mock_link = CYW43_LINK_UP;
    press(6);
    assert(session_current()->state == SESSION_RUNNING && session_current()->tests_performed == 1);
    assert(monitoring_result()->reason == MONITORING_INTERRUPTED);
    assert(monitoring_result()->session_id == mot_result()->session_id);
    assert(monitoring_result()->valid[0] > 10 && monitoring_result()->buffer_drops == 0);
    for (unsigned i = 0; i < 300 && telemetry_pending(); ++i) vTaskDelay(10);
    assert(communication_status() == COMM_ONLINE && telemetry_pending() == 0);
    assert(frames > 10 && mock_publications > 0);
    puts("PASS integração RTOS: cinco tarefas, menu, calibração, MOT+MPU, pausa, perda e recuperação MQTT");
    exit(0);
}

int main(void) {
    for (unsigned i = 0; i < 30; ++i) pins[i] = true;
    assert(display_task_init() && input_task_init() && mpu_task_init() && session_task_init());
    assert(xTaskCreate(user_script, "user", 2048, NULL, 1, NULL) == pdPASS);
    vTaskStartScheduler();
    return 1;
}
