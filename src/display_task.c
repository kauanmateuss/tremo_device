#include "display_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

typedef enum { DISPLAY_OLED, DISPLAY_MATRIX } display_target_t;

typedef struct {
    display_target_t target;
    const npLED_t *pixels;
    uint8_t *buffer;
    struct render_area *area;
    TaskHandle_t caller;
    bool *success;
    uint64_t *completed_us;
} display_request_t;

static QueueHandle_t requests;
static SemaphoreHandle_t transfer_mutex;

static void task_display(void *parameter) {
    (void)parameter;
    display_request_t request;
    for (;;) {
        if (xQueueReceive(requests, &request, portMAX_DELAY) == pdTRUE) {
            if (request.target == DISPLAY_OLED) {
                *request.success = render_on_display(request.buffer, request.area);
            } else {
                *request.completed_us = matrix_write_frame(request.pixels);
                *request.success = true;
            }
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

static bool transfer(display_request_t request) {
    bool success = false;
    BaseType_t taken = xSemaphoreTake(transfer_mutex, portMAX_DELAY);
    configASSERT(taken == pdTRUE);
    request.caller = xTaskGetCurrentTaskHandle();
    request.success = &success;
    BaseType_t sent = xQueueSend(requests, &request, portMAX_DELAY);
    configASSERT(sent == pdTRUE);
    // A notificação de índice 0 fica reservada para a conclusão desta chamada.
    uint32_t notified = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    configASSERT(notified == 1);
    xSemaphoreGive(transfer_mutex);
    return success;
}

bool display_task_render(uint8_t *buffer, struct render_area *area) {
    return transfer((display_request_t){
        .target = DISPLAY_OLED, .buffer = buffer, .area = area
    });
}

uint64_t display_task_matrix(const npLED_t *pixels) {
    uint64_t completed_us = 0;
    transfer((display_request_t){
        .target = DISPLAY_MATRIX, .pixels = pixels, .completed_us = &completed_us
    });
    return completed_us;
}
