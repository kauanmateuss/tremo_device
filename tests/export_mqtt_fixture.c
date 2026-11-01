// Reutiliza periféricos simulados e ações da FSM; publica JSON real do firmware.
#define main existing_flow_suite
#include "test_flow.c"
#undef main
#include "telemetry.h"
#include "communication.h"
void communication_init(void) { telemetry_init("0123456789abcdef", true); }
communication_status_t communication_status(void) { return COMM_ONLINE; }
void communication_update(communication_mode_t mode) {
    if (mode == COMM_SUSPENDED) return;
    while (telemetry_peek()) { puts(telemetry_peek()); telemetry_ack(); }
}
int main(void) {
    healthy[0] = healthy[1] = true;
    app_init(); step(CONTROL_NONE, 2000);
    session_config_t config = *session_config(); config.tests_mask = 3;
    assert(session_configure(config));
    step(CONTROL_DOWN, 5); step(CONTROL_DOWN, 5); step(CONTROL_CONFIRM, 5);
    step(CONTROL_CONFIRM, 5);
    step(CONTROL_CONFIRM, 5); step(CONTROL_NONE, 5); finish_calibration();
    step(CONTROL_CONFIRM, 5); step(CONTROL_NONE, 0);
    for (unsigned i = 0; i < 6000; ++i) step(CONTROL_NONE, 5);
    assert(app_get_state() == APP_RESULTS);
    step(CONTROL_CONFIRM, 5); complete_mot();
    step(CONTROL_CONFIRM, 5); complete_rti();
    step(CONTROL_CONFIRM, 5);
    assert(session_current()->state == SESSION_COMPLETE);
    assert(telemetry_dropped() == 0);
    return 0;
}
