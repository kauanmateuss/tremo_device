#ifndef SESSION_H
#define SESSION_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { SESSION_IDLE, SESSION_RUNNING, SESSION_PAUSED, SESSION_COMPLETE, SESSION_INTERRUPTED, SESSION_ERROR } session_state_t;
typedef enum { SESSION_TREMOR, SESSION_MOT, SESSION_RTI, SESSION_PRM, SESSION_PAL, SESSION_BATTERY, SESSION_FULL, SESSION_MANUAL } session_kind_t;
typedef struct {
    uint32_t duration_ms;
    uint16_t sample_ms;
    uint8_t tests_mask; // Bits 0..3: MOT, RTI, PRM, PAL, nessa ordem.
    bool tremor_first;
    bool cognitive_mpu; // Coleta simultânea aos testes cognitivos, após preparo e calibração.
} session_config_t;
typedef struct {
    uint64_t id, started_us, ended_us, paused_us, pause_started_us;
    char patient_id[17];
    uint32_t pauses, tests_performed;
    bool pause_requested;
    session_state_t state;
    session_kind_t kind;
    session_config_t config;
} session_t;
// ID = nonce aleatório do boot + contador. Não identifica paciente; não persiste após reset.
void session_init(uint32_t boot_nonce);
bool session_configure(session_config_t config);
const session_config_t *session_config(void);
bool session_begin(session_kind_t kind, uint64_t now_us);
bool session_begin_patient(session_kind_t kind, uint64_t now_us, const char *patient);
bool session_is_open(void);
void session_record_test(void);
void session_request_pause(void);
bool session_pause(uint64_t now_us);
uint64_t session_resume(uint64_t now_us);
void session_finish(session_state_t reason, uint64_t now_us);
uint64_t session_active_us(uint64_t now_us);
const session_t *session_current(void);
#endif
