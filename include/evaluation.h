#ifndef EVALUATION_H
#define EVALUATION_H
#include "session.h"
#define EVALUATION_MAX_STEPS 5
#define EVALUATION_TREMOR 4
typedef enum { STEP_PENDING, STEP_RUNNING, STEP_COMPLETE, STEP_LIMIT, STEP_INTERRUPTED, STEP_ERROR } step_status_t;
typedef struct {
    uint8_t module; // 0 MOT, 1 RTI, 2 PRM, 3 PAL, 4 tremores.
    step_status_t status;
    uint32_t correct, wrong, omissions;
} evaluation_step_t;
typedef struct {
    bool active;
    uint8_t count, index;
    uint64_t session_id;
    evaluation_step_t steps[EVALUATION_MAX_STEPS];
} evaluation_t;
bool evaluation_begin(bool full, const session_t *session);
void evaluation_start_step(void);
void evaluation_record(step_status_t status, uint32_t correct, uint32_t wrong, uint32_t omissions);
bool evaluation_next(void);
void evaluation_cancel(bool error);
const evaluation_t *evaluation_current(void);
#endif
