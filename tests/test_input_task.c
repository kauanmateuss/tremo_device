#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "input_task.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

static bool a_high = true;
static bool expect_overflow;
uint64_t time_us_64(void) { return (uint64_t)xTaskGetTickCount() * 1000; }
void gpio_init(uint pin) { (void)pin; }
void gpio_set_dir(uint pin, bool out) { (void)pin; (void)out; }
void gpio_pull_up(uint pin) { (void)pin; }
bool gpio_get(uint pin) { return pin == 5 ? a_high : true; }
void adc_init(void) {}
void adc_gpio_init(uint pin) { (void)pin; }
void adc_select_input(uint input) { (void)input; }
uint16_t adc_read(void) { return 2048; }
void panic(const char *format, ...) {
    (void)format;
    assert(expect_overflow);
    puts("PASS RTOS input: saturação detectada, sem perda silenciosa");
    exit(0);
}
void rtos_assert_failed(const char *file, int line) {
    fprintf(stderr, "RTOS assert %s:%d\n", file, line);
    abort();
}
void vApplicationMallocFailedHook(void) { abort(); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task; (void)name; abort();
}

static void consumer(void *parameter) {
    (void)parameter;
    controls_init();
    input_task_start();
    vTaskDelay(40);
    input_sample_t before = input_task_next();
    assert(before.event == CONTROL_NONE && before.button.settled);
    a_high = false;
    vTaskDelay(40);
    a_high = true;
    vTaskDelay(40);
    // A aplicação ficou bloqueada enquanto a TaskInput registrava as duas bordas.
    input_sample_t press = input_task_next();
    input_sample_t release = input_task_next();
    assert(press.event == CONTROL_CONFIRM && press.button.pressed);
    assert(release.event == CONTROL_CONFIRM_RELEASE && !release.button.pressed);
    assert(press.button.edge_us >= before.timestamp_us);
    assert(release.button.edge_us > press.button.edge_us);
    assert(release.button.edge_us < release.timestamp_us);
    assert(release.timestamp_us - release.button.edge_us >= 30000);
    input_sample_t current = input_task_next();
    assert(current.event == CONTROL_NONE && current.button.settled);
    assert(current.sequence - before.sequence >= 14);
    a_high = false;
    vTaskDelay(10);
    current = input_task_next();
    assert(current.event == CONTROL_NONE && !current.button.settled);
    puts("PASS RTOS input: amostragem independente, FIFO, bordas e debounce pendente");

    TickType_t before_wait = xTaskGetTickCount();
    current = input_task_wait(100);
    assert(current.event == CONTROL_CONFIRM && current.button.pressed);
    assert(xTaskGetTickCount() - before_wait < 100);
    before_wait = xTaskGetTickCount();
    current = input_task_wait(10);
    assert(current.event == CONTROL_NONE && current.button.settled);
    assert(xTaskGetTickCount() - before_wait >= 10);
    puts("PASS RTOS input: espera acorda por evento e por prazo");

    expect_overflow = true;
    // O consumidor deixa de drenar a fila. A falha deve ser explícita.
    for (unsigned i = 0; i < 40; ++i) {
        a_high = !a_high;
        vTaskDelay(40);
    }
    abort();
}

int main(void) {
    assert(input_task_init());
    assert(xTaskCreate(consumer, "consumer", 1024, NULL, 2, NULL) == pdPASS);
    vTaskStartScheduler();
    return 1;
}
