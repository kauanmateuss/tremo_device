#include <stdio.h>
#include "app.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

static void task_app(void *parameter) {
    (void)parameter;
    app_init();
    for (;;) {
        app_update();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void task_status(void *parameter) {
    (void)parameter;
    for (;;) {
        if (stdio_usb_connected()) {
            printf("# RTOS core=%u tick=%lu\n", get_core_num(),
                   (unsigned long)xTaskGetTickCount());
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void) {
    stdio_init_all();
    if (xTaskCreate(task_app, "TaskApp", 2048, NULL, 2, NULL) != pdPASS ||
        xTaskCreate(task_status, "TaskStatus", 512, NULL, 1, NULL) != pdPASS) {
        panic("FreeRTOS: falha ao criar tarefas");
    }
    vTaskStartScheduler();
    panic("FreeRTOS: scheduler retornou");
}
