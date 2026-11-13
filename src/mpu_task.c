#include "mpu_task.h"
#include "monitoring.h"
#include "FreeRTOS.h"
#include "task.h"

static TaskHandle_t mpu_handle;

static void task_mpu(void *parameter) {
    (void)parameter;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        monitoring_worker_update();
        if (xTaskDelayUntil(&wake, pdMS_TO_TICKS(1)) == pdFALSE) {
            vTaskDelay(1);
            wake = xTaskGetTickCount();
        }
    }
}

bool mpu_task_init(void) {
    return xTaskCreate(task_mpu, "TaskMPU", 768, NULL, 4, &mpu_handle) == pdPASS;
}

void mpu_task_start(void) {
    xTaskNotifyGive(mpu_handle);
}
