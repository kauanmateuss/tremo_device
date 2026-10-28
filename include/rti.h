#ifndef RTI_H
#define RTI_H

#include <stdbool.h>
#include <stdint.h>
#include "controls.h"

#define RTI_TRAINING_TRIALS 3
#define RTI_TEST_TRIALS 10
#define RTI_MAX_RECORDS (RTI_TRAINING_TRIALS + RTI_TEST_TRIALS)
#define RTI_MIN_WAIT_US 1000000u
#define RTI_MAX_WAIT_US 3000000u
#define RTI_RESPONSE_LIMIT_US 2000000ull
#define RTI_INTERVAL_US 1000000ull
// Permite validar uma borda anterior ao prazo, sem esperar indefinidamente por ruído.
#define RTI_DEBOUNCE_GRACE_US 50000ull

typedef enum {
    RTI_INSTRUCTIONS, RTI_WAIT_RELEASE, RTI_WAIT_HOLD, RTI_FOREPERIOD,
    RTI_STIMULUS, RTI_INTERVAL, RTI_READY, RTI_TRAINING_FAILED, RTI_RESULTS
} rti_state_t;
typedef enum { RTI_RESPONSE, RTI_ANTICIPATION, RTI_OMISSION, RTI_ABORTED } rti_outcome_t;

typedef struct {
    bool training, stimulus_shown, released;
    uint32_t planned_wait_us;
    uint64_t hold_us, onset_us, release_us, ended_us;
    rti_outcome_t outcome;
} rti_record_t;

typedef struct {
    bool interrupted;
    uint8_t responses, anticipations, omissions, record_count;
    uint64_t response_time_us;
    rti_record_t records[RTI_MAX_RECORDS];
} rti_result_t;

// Requer OLED/matriz inicializados. Protocolo local simples por soltura, não CANTAB oficial.
void rti_begin(void);
// true retorna ao menu. now_us é monotônico; botão deve vir do último controls_poll().
bool rti_update(control_event_t event, control_button_state_t button, uint64_t now_us);
void rti_serial_update(void);
rti_state_t rti_get_state(void);
const rti_result_t *rti_result(void);

#endif
