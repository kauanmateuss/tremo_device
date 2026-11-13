#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

void rtos_assert_failed(const char *file, int line) {
    taskDISABLE_INTERRUPTS();
    panic("FreeRTOS: assert em %s:%d", file, line);
}

void vApplicationMallocFailedHook(void) {
    taskDISABLE_INTERRUPTS();
    panic("FreeRTOS: heap insuficiente");
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task;
    taskDISABLE_INTERRUPTS();
    panic("FreeRTOS: stack excedida em %s", name);
}
