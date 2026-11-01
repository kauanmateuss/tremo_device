#include <assert.h>
#include <stdio.h>
#include "evaluation.h"
int main(void) {
    session_init(123); session_begin(SESSION_FULL, 0);
    assert(evaluation_begin(true, session_current()));
    assert(evaluation_current()->count == 5 && evaluation_current()->steps[0].module == 4);
    assert(!evaluation_next());
    for (unsigned i = 0; i < 5; ++i) {
        evaluation_start_step(); evaluation_record(STEP_COMPLETE, 1, 0, 0);
        assert(evaluation_next() == (i < 4));
    }
    assert(!evaluation_current()->active);
    session_finish(SESSION_COMPLETE, 100); session_config_t c = *session_config();
    c.tests_mask = 10; c.tremor_first = false; assert(session_configure(c));
    session_begin(SESSION_FULL, 200); assert(evaluation_begin(true, session_current()));
    assert(evaluation_current()->steps[0].module == 1 && evaluation_current()->steps[1].module == 3 && evaluation_current()->steps[2].module == 4);
    evaluation_start_step(); evaluation_record(STEP_COMPLETE, 4, 1, 0); assert(evaluation_next());
    evaluation_start_step(); evaluation_cancel(true);
    assert(evaluation_current()->steps[0].status == STEP_COMPLETE && evaluation_current()->steps[1].status == STEP_ERROR);
    assert(evaluation_current()->steps[2].status == STEP_PENDING);
    assert(evaluation_begin(false, session_current()) && evaluation_current()->count == 2);
    evaluation_start_step(); evaluation_record(STEP_LIMIT, 1, 4, 0); assert(evaluation_next());
    evaluation_start_step(); evaluation_record(STEP_INTERRUPTED, 0, 0, 0); assert(!evaluation_next());
    evaluation_cancel(false); assert(!evaluation_current()->active);
    puts("PASS sequencia: configuracao, ordem, transicoes, limite e interrupcao");
}
