#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "display_task.h"
#include "FreeRTOS.h"
#include "task.h"

static unsigned completed;
static TaskHandle_t client_a, client_b;

bool render_on_display(uint8_t *buffer, struct render_area *area) {
    assert(xTaskGetCurrentTaskHandle() != client_a);
    assert(xTaskGetCurrentTaskHandle() != client_b);
    uint8_t expected = *buffer;
    assert(area->buffer_length == expected);
    vTaskDelay(1);
    assert(*buffer == expected);
    return expected % 2 == 0;
}

uint64_t matrix_write_frame(const npLED_t *pixels) {
    assert(xTaskGetCurrentTaskHandle() != client_a);
    assert(xTaskGetCurrentTaskHandle() != client_b);
    uint8_t expected = pixels[0].R;
    vTaskDelay(1);
    assert(pixels[0].R == expected);
    return (uint64_t)expected + 1000;
}

void rtos_assert_failed(const char *file, int line) {
    fprintf(stderr, "RTOS assert %s:%d\n", file, line);
    abort();
}
void vApplicationMallocFailedHook(void) { abort(); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task; (void)name; abort();
}

static void client(void *parameter) {
    unsigned offset = (unsigned)(uintptr_t)parameter;
    for (unsigned i = 0; i < 100; ++i) {
        uint8_t value = (uint8_t)(offset + i);
        struct render_area area = {.buffer_length = value};
        assert(display_task_render(&value, &area) == (value % 2 == 0));
        npLED_t pixels[LED_COUNT] = {{.R = value}};
        assert(display_task_matrix(pixels) == (uint64_t)value + 1000);
    }
    taskENTER_CRITICAL();
    if (++completed == 2) {
        puts("PASS RTOS display: dois produtores, buffer estável, confirmação e erro I2C");
        exit(0);
    }
    taskEXIT_CRITICAL();
    for (;;) vTaskDelay(1000);
}

int main(void) {
    assert(display_task_init());
    assert(xTaskCreate(client, "clientA", 1024, (void *)1, 2, &client_a) == pdPASS);
    assert(xTaskCreate(client, "clientB", 1024, (void *)2, 2, &client_b) == pdPASS);
    vTaskStartScheduler();
    return 1;
}
