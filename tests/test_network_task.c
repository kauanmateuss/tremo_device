#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "communication.h"
#include "telemetry.h"
#include "session.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "FreeRTOS.h"
#include "task.h"

#include "network_fixture.h"

static void producer(void *parameter) {
    (void)parameter;
    communication_init();
    session_init(1); assert(session_begin(SESSION_MOT, time_us_64()));
    for (unsigned i = 0; i < 20; ++i) {
        telemetry_emit(session_current()->id, time_us_64(), "trial", true, "{\"index\":%u}", i);
        vTaskDelay(5); // Continua produzindo durante a inicialização lenta do rádio.
    }
    vTaskDelay(500);
    assert(mock_polls && mock_publications == 21 && telemetry_pending() == 0);
    assert(communication_status() == COMM_ONLINE);
    mock_link = 0;
    vTaskDelay(100);
    assert(mock_joins == 1 && communication_status() != COMM_ONLINE);
    for (unsigned i = 0; i < 120; ++i)
        telemetry_emit(session_current()->id, time_us_64(), "trial", true, "{}");
    assert(telemetry_pending() == TELEMETRY_CAPACITY && telemetry_dropped() == 24);
    taskENTER_CRITICAL(); mock_offset_us += 16000000; taskEXIT_CRITICAL();
    communication_update(COMM_SUSPENDED); // A FSM não paralisa a tarefa de rede.
    vTaskDelay(100); assert(mock_joins >= 2);
    mock_link = CYW43_LINK_UP;
    vTaskDelay(1500);
    assert(communication_status() == COMM_ONLINE && telemetry_pending() == 0);
    assert(mock_publications == 21 + TELEMETRY_CAPACITY);
    puts("PASS RTOS rede: dono único, produção durante conexão lenta, fila cheia e recuperação");
    exit(0);
}

int main(void) {
    assert(xTaskCreate(producer, "session", 2048, NULL, 2, NULL) == pdPASS);
    vTaskStartScheduler();
    return 1;
}
