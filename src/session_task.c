#include <stdio.h>
#include "session_task.h"
#include "app.h"
#include "mpu_task.h"
#include "communication.h"
#include "session.h"
#include "input_task.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

static void report_memory(void) {
    // Cinco tarefas da aplicação, idle, timers e uma posição de reserva.
    TaskStatus_t tasks[8];
    UBaseType_t count = uxTaskGetSystemState(tasks, 8, NULL);
    printf("# RTOS_MEM heap_free=%lu heap_min=%lu tasks=%u\n",
           (unsigned long)xPortGetFreeHeapSize(),
           (unsigned long)xPortGetMinimumEverFreeHeapSize(), (unsigned)count);
    for (UBaseType_t i = 0; i < count; ++i) {
        printf("# RTOS_STACK name=%s free_min_bytes=%lu\n", tasks[i].pcTaskName,
               (unsigned long)tasks[i].usStackHighWaterMark * sizeof(StackType_t));
    }
}

static void task_session(void *parameter) {
    (void)parameter;
    app_init();
    mpu_task_start();
    input_task_start();
    TickType_t last_status = xTaskGetTickCount();
    TickType_t last_memory = last_status;
    for (;;) {
        input_sample_t input = input_task_wait(5);
        app_update_captured(input.event, input.button, input.timestamp_us, time_us_64());
        TickType_t now = xTaskGetTickCount();
        if (now - last_status >= pdMS_TO_TICKS(1000)) {
            last_status = now;
            if (stdio_usb_connected()) {
                printf("# RTOS core=%u tick=%lu app=%u input=%lu net=%u heap=%lu\n", get_core_num(),
                       (unsigned long)now, app_get_state(), (unsigned long)input.sequence, communication_status(),
                       (unsigned long)xPortGetFreeHeapSize());
            }
        }
        session_state_t state = session_current()->state;
        if (state != SESSION_RUNNING && state != SESSION_PAUSED &&
            now - last_memory >= pdMS_TO_TICKS(5000)) {
            last_memory = now;
            if (stdio_usb_connected()) report_memory();
        }
    }
}

bool session_task_init(void) {
    return xTaskCreate(task_session, "TaskSession", 2048, NULL, 2, NULL) == pdPASS;
}
