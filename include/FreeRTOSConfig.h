#ifndef TREMO_FREERTOS_CONFIG_H
#define TREMO_FREERTOS_CONFIG_H

#define configNUMBER_OF_CORES           1
#define configUSE_PREEMPTION            1
#define configTICK_RATE_HZ              1000
#define configTICK_TYPE_WIDTH_IN_BITS   TICK_TYPE_WIDTH_32_BITS
#define configMAX_PRIORITIES            5
#define configMINIMAL_STACK_SIZE        256  /* Palavras de 32 bits da tarefa idle. */
#define configTOTAL_HEAP_SIZE           (16 * 1024)
#define configUSE_IDLE_HOOK             0
#define configUSE_TICK_HOOK             0

/* O laço atual usa o SDK antes da partida do scheduler (Task 03). */
#define configSUPPORT_PICO_TIME_INTEROP 0
#define configSUPPORT_PICO_SYNC_INTEROP 0

#endif
