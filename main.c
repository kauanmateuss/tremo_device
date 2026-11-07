#include <stdio.h>
#include "app.h"
#include "display_task.h"
#include "input_task.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

static void task_app(void *parameter) {
    (void)parameter;
    app_init();
    input_task_start();
    TickType_t last_status = xTaskGetTickCount();
    for (;;) {
        input_sample_t input = input_task_next();
        // Avança os prazos no instante atual. O RTI usa a borda capturada em button.
        app_update_input(input.event, input.button, time_us_64());
        TickType_t now = xTaskGetTickCount();
        if (now - last_status >= pdMS_TO_TICKS(1000)) {
            last_status = now;
            if (stdio_usb_connected()) {
                printf("# RTOS core=%u tick=%lu app=%u input=%lu heap=%lu\n", get_core_num(),
                       (unsigned long)now, app_get_state(), (unsigned long)input.sequence,
                       (unsigned long)xPortGetFreeHeapSize());
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

int main(void) {
    stdio_init_all();
    if (!display_task_init() || !input_task_init() ||
        xTaskCreate(task_app, "TaskApp", 2048, NULL, 2, NULL) != pdPASS) {
        panic("FreeRTOS: falha ao criar tarefas");
    }
    vTaskStartScheduler();
    panic("FreeRTOS: scheduler retornou");
}
