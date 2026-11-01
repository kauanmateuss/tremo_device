#ifndef PRM_H
#define PRM_H

#include <stdbool.h>
#include <stdint.h>
#include "controls.h"

#define PRM_TRAINING_TRIALS 2
#define PRM_TEST_TRIALS 6
#define PRM_MAX_RECORDS (PRM_TRAINING_TRIALS + PRM_TEST_TRIALS)
#define PRM_ENCODING_US 2000000ull
#define PRM_GAP_US 500000ull
#define PRM_RETENTION_US 1000000ull
#define PRM_TIMEOUT_US 15000000ull
#define PRM_INTERVAL_US 1000000ull
#define PRM_NO_CHOICE 255

typedef enum {
    PRM_INSTRUCTIONS, PRM_ENCODING, PRM_GAP, PRM_RETENTION, PRM_CHOICE,
    PRM_INTERVAL, PRM_READY, PRM_TRAINING_FAILED, PRM_RESULTS
} prm_state_t;
typedef enum { PRM_CORRECT, PRM_WRONG, PRM_OMISSION, PRM_ABORTED } prm_outcome_t;
typedef struct {
    bool training;
    uint8_t target, novel, target_option, selected_option;
    prm_outcome_t outcome;
    uint64_t onset_us, elapsed_us;
} prm_record_t;
typedef struct {
    bool interrupted;
    uint8_t correct, wrong, omissions, record_count;
    uint64_t correct_time_us;
    prm_record_t records[PRM_MAX_RECORDS];
} prm_result_t;

// Requer OLED/matriz inicializados. Tempos medem escolha incluindo navegação.
void prm_begin(void);
bool prm_update(control_event_t event, uint64_t event_us);
void prm_serial_update(void);
prm_state_t prm_get_state(void);
const prm_result_t *prm_result(void);
#endif
