#include "display_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

typedef struct {
    uint8_t *buffer;
    struct render_area *area;
    TaskHandle_t caller;
    bool *success;
} display_request_t;

static QueueHandle_t requests;
static SemaphoreHandle_t transfer_mutex;

static void task_display(void *parameter) {
    (void)parameter;
    display_request_t request;
    for (;;) {
        if (xQueueReceive(requests, &request, portMAX_DELAY) == pdTRUE) {
            *request.success = render_on_display(request.buffer, request.area);
            xTaskNotifyGive(request.caller);
        }
    }
}

bool display_task_init(void) {
    requests = xQueueCreate(1, sizeof(display_request_t));
    transfer_mutex = xSemaphoreCreateMutex();
    if (!requests || !transfer_mutex) return false;
    return xTaskCreate(task_display, "TaskDisplay", 1024, NULL, 2, NULL) == pdPASS;
}

bool display_task_render(uint8_t *buffer, struct render_area *area) {
    bool success = false;
    configASSERT(xSemaphoreTake(transfer_mutex, portMAX_DELAY) == pdTRUE);
    display_request_t request = {
        .buffer = buffer, .area = area,
        .caller = xTaskGetCurrentTaskHandle(), .success = &success
    };
    configASSERT(xQueueSend(requests, &request, portMAX_DELAY) == pdTRUE);
    // A notificação de índice 0 fica reservada para a conclusão desta chamada.
    configASSERT(ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == 1);
    xSemaphoreGive(transfer_mutex);
    return success;
}
