#include "input_task.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

static QueueHandle_t events;
static TaskHandle_t input_handle;
static input_sample_t latest;

static void task_input(void *parameter) {
    (void)parameter;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    TickType_t wake = xTaskGetTickCount();
    uint32_t sequence = 0;
    for (;;) {
        control_event_t event = controls_poll((uint32_t)(time_us_64() / 1000));
        input_sample_t sample = {
            .event = event, .button = controls_confirm_state(),
            .timestamp_us = time_us_64(), .sequence = ++sequence
        };
        taskENTER_CRITICAL();
        latest = sample;
        BaseType_t sent = pdTRUE;
        if (event != CONTROL_NONE) {
            sent = xQueueSend(events, &sample, 0);
        }
        taskEXIT_CRITICAL();
        if (sent != pdTRUE) panic("FreeRTOS: fila de entradas cheia");
        // Um atraso prolongado reinicia a referência, evitando leituras em rajada.
        if (xTaskDelayUntil(&wake, pdMS_TO_TICKS(5)) == pdFALSE) {
            vTaskDelay(1);
            wake = xTaskGetTickCount();
        }
    }
}

bool input_task_init(void) {
    events = xQueueCreate(32, sizeof(input_sample_t));
    if (!events) return false;
    return xTaskCreate(task_input, "TaskInput", 512, NULL, 3, &input_handle) == pdPASS;
}

void input_task_start(void) {
    xTaskNotifyGive(input_handle);
}

input_sample_t input_task_next(void) {
    input_sample_t sample;
    taskENTER_CRITICAL();
    if (xQueueReceive(events, &sample, 0) != pdTRUE) {
        sample = latest;
        sample.event = CONTROL_NONE;
    }
    taskEXIT_CRITICAL();
    return sample;
}

input_sample_t input_task_wait(uint32_t timeout_ms) {
    input_sample_t sample;
    if (xQueueReceive(events, &sample, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return sample;
    }
    // Reconfere a fila junto com o snapshot caso uma entrada chegue no timeout.
    return input_task_next();
}
