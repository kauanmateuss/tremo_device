#include "session.h"
#include "telemetry.h"
#include <stdio.h>
static session_t current;
static session_config_t config;
static uint32_t nonce, sequence;
void session_init(uint32_t boot_nonce) {
    nonce = boot_nonce; sequence = 0;
    current = (session_t){0};
    config = (session_config_t){.duration_ms = 30000, .sample_ms = 40, .tests_mask = 15, .tremor_first = true};
}
bool session_configure(session_config_t value) {
    if (current.state == SESSION_RUNNING || current.state == SESSION_PAUSED) return false;
    if ((value.duration_ms != 30000 && value.duration_ms != 60000) ||
        (value.sample_ms != 20 && value.sample_ms != 40) || !value.tests_mask || value.tests_mask > 15) return false;
    config = value; return true;
}
const session_config_t *session_config(void) { return &config; }
bool session_begin(session_kind_t kind, uint64_t now_us) {
    return session_begin_patient(kind, now_us, "");
}
bool session_begin_patient(session_kind_t kind, uint64_t now_us, const char *patient) {
    if (current.state == SESSION_RUNNING || current.state == SESSION_PAUSED || sequence == UINT32_MAX) return false;
    current = (session_t){.id = ((uint64_t)nonce << 32) | ++sequence, .started_us = now_us,
        .state = SESSION_RUNNING, .kind = kind, .config = config};
    snprintf(current.patient_id, sizeof(current.patient_id), "%s", patient);
    telemetry_start(&current);
    return true;
}
void session_request_pause(void) { if (current.state == SESSION_RUNNING) current.pause_requested = true; }
bool session_pause(uint64_t now_us) {
    if (current.state != SESSION_RUNNING) return false;
    current.state = SESSION_PAUSED; current.pause_started_us = now_us;
    current.pause_requested = false; ++current.pauses; telemetry_state(&current, now_us, false); return true;
}
uint64_t session_resume(uint64_t now_us) {
    if (current.state != SESSION_PAUSED) return 0;
    uint64_t elapsed = now_us - current.pause_started_us;
    current.paused_us += elapsed; current.state = SESSION_RUNNING; telemetry_state(&current, now_us, false); return elapsed;
}
void session_finish(session_state_t reason, uint64_t now_us) {
    if (reason != SESSION_COMPLETE && reason != SESSION_INTERRUPTED && reason != SESSION_ERROR) return;
    if (current.state != SESSION_RUNNING && current.state != SESSION_PAUSED) return;
    if (current.state == SESSION_PAUSED) session_resume(now_us);
    current.ended_us = now_us; current.state = reason; current.pause_requested = false;
    telemetry_state(&current, now_us, true);
}
uint64_t session_active_us(uint64_t now_us) {
    if (current.state == SESSION_IDLE) return 0;
    uint64_t end = current.state == SESSION_RUNNING ? now_us :
        current.state == SESSION_PAUSED ? current.pause_started_us : current.ended_us;
    return end - current.started_us - current.paused_us;
}
const session_t *session_current(void) { return &current; }
