#include "assessment.h"
#include "session.h"
#include "monitoring.h"
#include "display.h"
#include "mot.h"
#include "rti.h"
#include "prm.h"
#include "pal.h"
#include "pico/stdlib.h"
static unsigned selected;
static bool safe_boundary(void) {
    switch (selected) {
    case 0: return mot_get_state() == MOT_INSTRUCTIONS || mot_get_state() == MOT_INTERVAL || mot_get_state() == MOT_READY || mot_get_state() == MOT_TRAINING_FAILED;
    case 1: return rti_get_state() == RTI_INSTRUCTIONS || rti_get_state() == RTI_INTERVAL || rti_get_state() == RTI_READY || rti_get_state() == RTI_TRAINING_FAILED;
    case 2: return prm_get_state() == PRM_INSTRUCTIONS || prm_get_state() == PRM_READY || prm_get_state() == PRM_TRAINING_FAILED;
    default: return pal_get_state() == PAL_INSTRUCTIONS || pal_get_state() == PAL_READY || pal_get_state() == PAL_TRAINING_FAILED || pal_get_state() == PAL_RETRY || pal_get_state() == PAL_NEXT_LEVEL;
    }
}
bool assessment_finished(void) {
    switch (selected) {
    case 0: return mot_get_state() == MOT_RESULTS;
    case 1: return rti_get_state() == RTI_RESULTS;
    case 2: return prm_get_state() == PRM_RESULTS;
    default: return pal_get_state() == PAL_RESULTS;
    }
}
bool assessment_interrupted(void) {
    switch (selected) {
    case 0: return mot_result()->interrupted;
    case 1: return rti_result()->interrupted;
    case 2: return prm_result()->interrupted;
    default: return pal_result()->interrupted;
    }
}
void assessment_begin(unsigned test) {
    selected = test;
    switch (selected) { case 0: mot_begin(); break; case 1: rti_begin(); break; case 2: prm_begin(); break; default: pal_begin(); break; }
}
static bool pause_if_safe(void) {
    if (!session_current()->pause_requested || !safe_boundary()) return false;
    display_save_screen();
    const char *lines[] = {"TESTE PAUSADO", "", "A CONTINUAR", "B PARAR TESTE"};
    session_pause(time_us_64());
    if (session_current()->config.cognitive_mpu)
        monitoring_pause(to_ms_since_boot(get_absolute_time()));
    display_show_lines(lines, 4); return true;
}
bool assessment_update(control_event_t event, control_button_state_t button, uint64_t event_us, uint64_t now_us) {
    if (session_current()->state == SESSION_PAUSED) {
        if (event != CONTROL_CONFIRM && event != CONTROL_BACK) return false;
        display_restore_screen();
        uint64_t delta = session_resume(time_us_64());
        if (session_current()->config.cognitive_mpu)
            monitoring_resume(to_ms_since_boot(get_absolute_time()));
        if (selected == 0) mot_resume_after_pause(delta);
        if (selected == 1) rti_resume_after_pause(delta);
        now_us = time_us_64();
        if (event == CONTROL_CONFIRM) return false;
    }
    if (event == CONTROL_PAUSE && !assessment_finished()) session_request_pause();
    if (event != CONTROL_BACK && pause_if_safe()) return false;
    bool exit = false;
    switch (selected) {
    case 0: exit = mot_update_captured(event, event_us, now_us); mot_serial_update(); break;
    case 1: exit = rti_update(event, button, now_us); rti_serial_update(); break;
    case 2: exit = prm_update_captured(event, event_us, now_us); prm_serial_update(); break;
    default: exit = pal_update_captured(event, event_us, now_us); pal_serial_update(); break;
    }
    if (!assessment_finished()) pause_if_safe();
    return exit;
}
