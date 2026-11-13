#ifndef TREMO_FREERTOS_CONFIG_H
#define TREMO_FREERTOS_CONFIG_H

#define configNUMBER_OF_CORES           1
#define configUSE_PREEMPTION            1
#define configTICK_RATE_HZ              1000
#define configTICK_TYPE_WIDTH_IN_BITS   TICK_TYPE_WIDTH_32_BITS
#define configMAX_PRIORITIES            5
#define configMINIMAL_STACK_SIZE        256  /* Palavras de 32 bits da tarefa idle. */
#define configTOTAL_HEAP_SIZE           (24 * 1024)
#define configUSE_IDLE_HOOK             0
#define configUSE_TICK_HOOK             0

/* Esperas do SDK continuam independentes. A aplicação usa vTaskDelay. */
#define INCLUDE_xTimerPendFunctionCall  1
#define INCLUDE_xTaskDelayUntil        1
#define INCLUDE_vTaskDelay             1
#define configUSE_MUTEXES              1
#define INCLUDE_vTaskSuspend           1
#define configUSE_TIMERS               1
#define configTIMER_TASK_PRIORITY      3
#define configTIMER_QUEUE_LENGTH       8
#define configTIMER_TASK_STACK_DEPTH   256
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK   1

#ifdef __cplusplus
extern "C" {
#endif
void rtos_assert_failed(const char *file, int line);
#ifdef __cplusplus
}
#endif
#define configASSERT(condition) do { if (!(condition)) rtos_assert_failed(__FILE__, __LINE__); } while (0)

#define configSUPPORT_PICO_TIME_INTEROP 0
#define configSUPPORT_PICO_SYNC_INTEROP 1

#endif
