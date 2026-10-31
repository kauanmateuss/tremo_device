#ifndef MONITORING_H
#define MONITORING_H

#include <stdbool.h>
#include <stdint.h>

#define MONITORING_SAMPLE_MS 40
#define MONITORING_DURATION_MS 30000

typedef enum {
    MONITORING_COMPLETE, MONITORING_INTERRUPTED, MONITORING_SENSOR_ERROR
} monitoring_end_t;

typedef struct {
    uint64_t session_id;
    uint32_t duration_ms;
    uint32_t valid[2];
    uint32_t errors[2];
    monitoring_end_t reason;
} monitoring_result_t;

void monitoring_init(void);
// Preparação bloqueante (~400 ms com dois sensores), fora da aquisição.
// Verifica comunicação e leitura de ambos. Não registra amostras da sessão.
bool monitoring_prepare(void);
bool monitoring_sensor_ready(unsigned int index);
void monitoring_start(uint32_t now_ms);
void monitoring_pause(uint32_t now_ms);
void monitoring_resume(uint32_t now_ms);
// Agenda uma leitura se estiver no prazo. false indica falha de aquisição.
bool monitoring_update(uint32_t now_ms);
void monitoring_draw(void);
void monitoring_stop(uint32_t now_ms, monitoring_end_t reason);
const monitoring_result_t *monitoring_result(void);
// Mantém resultados em RAM e publica o último resumo ao reconectar o USB.
void monitoring_serial_update(void);

#endif
