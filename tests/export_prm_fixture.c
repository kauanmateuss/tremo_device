#define main existing_prm_suite
#include "test_prm.c"
#undef main
#include "telemetry.h"
int main(void) {
    telemetry_init("0123456789abcdef", true); session_init(2);
    assert(session_begin(SESSION_PRM, clock_us));
    prm_begin(); step(CONTROL_CONFIRM, 0); training(); step(CONTROL_CONFIRM, 0);
    encode(6);
    for (unsigned i = 0; i < 6; ++i) choose(i, 6, i == 1);
    assert(prm_get_state() == PRM_RESULTS);
    session_finish(SESSION_COMPLETE, clock_us);
    while (telemetry_peek()) { puts(telemetry_peek()); telemetry_ack(); }
    return 0;
}
