#include <stdio.h>
#include "session_task.h"
#include "app.h"
#include "mpu_task.h"
#include "input_task.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

static void task_session(void *parameter) {
    (void)parameter;
    app_init();
    mpu_task_start();
    input_task_start();
    TickType_t last_status = xTaskGetTickCount();
    for (;;) {
        input_sample_t input = input_task_wait(5);
        app_update_captured(input.event, input.button, input.timestamp_us, time_us_64());
        TickType_t now = xTaskGetTickCount();
        if (now - last_status >= pdMS_TO_TICKS(1000)) {
            last_status = now;
            if (stdio_usb_connected()) {
                printf("# RTOS core=%u tick=%lu app=%u input=%lu heap=%lu\n", get_core_num(),
                       (unsigned long)now, app_get_state(), (unsigned long)input.sequence,
                       (unsigned long)xPortGetFreeHeapSize());
            }
        }
    }
}

bool session_task_init(void) {
    return xTaskCreate(task_session, "TaskSession", 2048, NULL, 2, NULL) == pdPASS;
}
