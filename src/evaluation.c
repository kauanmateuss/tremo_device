#include "evaluation.h"
static evaluation_t current;
static void append(uint8_t module) { current.steps[current.count++].module = module; }
bool evaluation_begin(bool full, const session_t *session) {
    if (current.active || !session || !session->config.tests_mask || session->config.tests_mask > 15) return false;
    current = (evaluation_t){.active = true, .session_id = session->id};
    if (full && session->config.tremor_first) append(EVALUATION_TREMOR);
    for (uint8_t i = 0; i < 4; ++i) if (session->config.tests_mask & (1u << i)) append(i);
    if (full && !session->config.tremor_first) append(EVALUATION_TREMOR);
    return true;
}
void evaluation_start_step(void) {
    if (current.active && current.steps[current.index].status == STEP_PENDING) current.steps[current.index].status = STEP_RUNNING;
}
void evaluation_record(step_status_t status, uint32_t correct, uint32_t wrong, uint32_t omissions) {
    if (!current.active || current.steps[current.index].status != STEP_RUNNING || status < STEP_COMPLETE || status > STEP_ERROR) return;
    evaluation_step_t *s = &current.steps[current.index];
    s->status = status; s->correct = correct; s->wrong = wrong; s->omissions = omissions;
}
bool evaluation_next(void) {
    if (!current.active) return false;
    step_status_t status = current.steps[current.index].status;
    if (status != STEP_COMPLETE && status != STEP_LIMIT) return false;
    if (current.index + 1 == current.count) { current.active = false; return false; }
    ++current.index; return true;
}
void evaluation_cancel(bool error) {
    if (!current.active) return;
    if (current.steps[current.index].status == STEP_RUNNING) current.steps[current.index].status = error ? STEP_ERROR : STEP_INTERRUPTED;
    current.active = false;
}
const evaluation_t *evaluation_current(void) { return &current; }
