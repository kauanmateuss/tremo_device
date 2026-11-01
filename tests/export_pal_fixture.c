#define main existing_pal_suite
#include "test_pal.c"
#undef main
#include "telemetry.h"
int main(void) {
    telemetry_init("0123456789abcdef", true); session_init(3);
    assert(session_begin(SESSION_PAL, clock_us)); train();
    for (unsigned count = 2; count <= 4; ++count) {
        step(CONTROL_CONFIRM, 0);
        for (unsigned round = 1; round <= PAL_MAX_ROUNDS; ++round) {
            encode(count, round > 1);
            for (unsigned i = 0; i < count; ++i)
                respond(count, !i && round < 3 ? PAL_WRONG : PAL_CORRECT);
            if (round < 3) step(CONTROL_CONFIRM, 0);
        }
    }
    assert(pal_result()->record_count == 29 && pal_get_state() == PAL_RESULTS);
    session_finish(SESSION_COMPLETE, clock_us);
    while (telemetry_peek()) { puts(telemetry_peek()); telemetry_ack(); }
    return 0;
}
