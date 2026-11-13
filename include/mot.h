#ifndef MOT_H
#define MOT_H

#include <stdbool.h>
#include <stdint.h>
#include "controls.h"

#define MOT_TRAINING_TRIALS 3
#define MOT_TEST_TRIALS 10
#define MOT_TIMEOUT_US 10000000ull
#define MOT_INTERVAL_US 1000000ull
#define MOT_MAX_RECORDS (MOT_TRAINING_TRIALS + MOT_TEST_TRIALS)

typedef enum {
    MOT_INSTRUCTIONS, MOT_STIMULUS, MOT_INTERVAL, MOT_READY,
    MOT_TRAINING_FAILED, MOT_RESULTS
} mot_state_t;
typedef enum { MOT_CORRECT, MOT_WRONG, MOT_OMISSION, MOT_ABORTED } mot_outcome_t;

typedef struct {
    bool training;
    uint8_t target_x, target_y, cursor_x, cursor_y;
    mot_outcome_t outcome;
    uint64_t onset_us;
    uint64_t elapsed_us; // Latência em respostas; tempo decorrido em omissão/aborto.
} mot_record_t;

typedef struct {
    uint64_t session_id;
    bool interrupted;
    uint8_t correct, wrong, omissions;
    uint64_t correct_time_us;
    uint8_t record_count;
    mot_record_t records[MOT_MAX_RECORDS];
} mot_result_t;

// Requer matriz e OLED inicializados. A TaskSession coordena a coleta MPU.
void mot_begin(void);
// Só após pausa em fronteira segura; desloca o relógio do intervalo.
void mot_resume_after_pause(uint64_t delta_us);
// true pede retorno ao menu cognitivo. event_us é o instante de detecção do evento.
bool mot_update(control_event_t event, uint64_t event_us);
// Separa o timestamp de captura do instante em que a FSM processa a entrada.
bool mot_update_captured(control_event_t event, uint64_t event_us, uint64_t now_us);
// Exporta registros retidos ao conectar USB, somente fora de uma execução ativa.
void mot_serial_update(void);
mot_state_t mot_get_state(void);
const mot_result_t *mot_result(void);

#endif
