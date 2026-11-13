#ifndef INPUT_TASK_H
#define INPUT_TASK_H

#include "controls.h"

typedef struct {
    control_event_t event;
    control_button_state_t button;
    uint64_t timestamp_us;
    uint32_t sequence;
} input_sample_t;

bool input_task_init(void);
// Chamar após controls_init(), antes de consumir entradas.
void input_task_start(void);
// Retorna o próximo evento pendente ou a última amostra sem evento.
input_sample_t input_task_next(void);

#endif
