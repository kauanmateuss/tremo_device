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
    uint32_t missed_slots, buffer_drops, max_lateness_ms, max_read_us, max_pair_skew_us;
    int16_t gyro_bias[2][3];
    uint16_t sample_ms;
    bool calibrated;
} monitoring_result_t;

#define MONITORING_BUFFER_SIZE 256
#define MONITORING_CAL_SAMPLES 50
typedef enum { CAL_IDLE, CAL_RUNNING, CAL_OK, CAL_MOVING, CAL_SENSOR_ERROR } calibration_state_t;
typedef struct {
    uint64_t session_id, timestamp_us;
    uint32_t sequence, read_us;
    uint8_t sensor;
    bool valid;
    int16_t accel[3], gyro_raw[3];
    int32_t gyro[3]; // Contagens corrigidas; dividir por 131 para graus/s.
} monitoring_sample_t;
void monitoring_calibration_begin(uint32_t now_ms);
calibration_state_t monitoring_calibration_update(uint32_t now_ms);
void monitoring_calibration_cancel(void);
// Remove uma amostra da fila; um consumidor por vez (USB agora, MQTT futuramente).
bool monitoring_pop(monitoring_sample_t *sample);
void monitoring_init(void);
// Preparação bloqueante (~400 ms com dois sensores), fora da aquisição.
// Verifica comunicação e leitura de ambos. Não registra amostras da sessão.
bool monitoring_prepare(void);
bool monitoring_sensor_ready(unsigned int index);
void monitoring_start(uint32_t now_ms);
void monitoring_pause(uint32_t now_ms);
void monitoring_resume(uint32_t now_ms);
// No firmware RTOS, consulta a saúde da captura feita pela TaskMPU.
// Na execução síncrona de testes, agenda a leitura. false indica falha.
bool monitoring_update(uint32_t now_ms);
// Chamada exclusivamente pela TaskMPU para atender prazos de aquisição.
void monitoring_worker_update(void);
void monitoring_draw(void);
uint32_t monitoring_elapsed_ms(uint32_t now_ms);
void monitoring_stop(uint32_t now_ms, monitoring_end_t reason);
// Snapshot para o consumidor TaskSession, válido até a próxima chamada.
const monitoring_result_t *monitoring_result(void);
// Mantém resultados em RAM e publica o último resumo ao reconectar o USB.
void monitoring_serial_update(void);

#endif
