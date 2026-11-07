#include "display_task.h"
#include "input_task.h"
#include "session_task.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

int main(void) {
    stdio_init_all();
    if (!display_task_init() || !input_task_init() || !session_task_init()) {
        panic("FreeRTOS: falha ao criar tarefas");
    }
    vTaskStartScheduler();
    panic("FreeRTOS: scheduler retornou");
}
