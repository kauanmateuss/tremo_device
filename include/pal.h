#ifndef PAL_H
#define PAL_H

#include <stdbool.h>
#include <stdint.h>
#include "controls.h"

#define PAL_TRAINING_PAIRS 2
#define PAL_MAX_PAIRS 4
#define PAL_MAX_ROUNDS 3
#define PAL_MAX_RECORDS (PAL_TRAINING_PAIRS + (2 + 3 + 4) * PAL_MAX_ROUNDS)
#define PAL_ENCODING_US 2000000ull
#define PAL_GAP_US 500000ull
#define PAL_RETENTION_US 1000000ull
#define PAL_TIMEOUT_US 15000000ull
#define PAL_INTERVAL_US 1000000ull
#define PAL_NO_POSITION 255

typedef enum {
    PAL_INSTRUCTIONS, PAL_ENCODING, PAL_GAP, PAL_RETENTION, PAL_CHOICE,
    PAL_INTERVAL, PAL_READY, PAL_TRAINING_FAILED, PAL_RETRY, PAL_NEXT_LEVEL, PAL_RESULTS
} pal_state_t;
typedef enum { PAL_CORRECT, PAL_WRONG, PAL_OMISSION, PAL_ABORTED } pal_outcome_t;
typedef struct {
    bool training;
    uint8_t pairs, round, trial, pattern, target_position, selected_position;
    pal_outcome_t outcome;
    uint64_t onset_us, elapsed_us;
} pal_record_t;
typedef struct {
    uint64_t session_id;
    bool interrupted, limit_reached;
    uint8_t correct, wrong, omissions, levels_completed, rounds_started, record_count;
    uint64_t correct_time_us;
    pal_record_t records[PAL_MAX_RECORDS];
} pal_result_t;

// Posições 0/1 = superior esq/dir; 2/3 = inferior esq/dir. Treino fora das métricas.
void pal_begin(void);
bool pal_update(control_event_t event, uint64_t event_us);
// Captura da entrada separada do relógio usado para avançar memorização/intervalos.
bool pal_update_captured(control_event_t event, uint64_t event_us, uint64_t now_us);
void pal_serial_update(void);
pal_state_t pal_get_state(void);
const pal_result_t *pal_result(void);
#endif
