#ifndef TELEMETRY_H
#define TELEMETRY_H
#include <stdbool.h>
#include <stdint.h>
#include "session.h"
#define TELEMETRY_CAPACITY 96
#define TELEMETRY_PAYLOAD 768
// Fila de um único produtor/consumidor no loop principal. Só ACK remove o primeiro.
void telemetry_init(const char *device, bool enabled);
bool telemetry_enabled(void);
void telemetry_start(const session_t *session);
void telemetry_state(const session_t *session, uint64_t now_us, bool terminal);
void telemetry_emit(uint64_t session_id, uint64_t time_us, const char *type, bool essential, const char *format, ...);
const char *telemetry_peek(void);
void telemetry_ack(void);
unsigned telemetry_pending(void);
uint32_t telemetry_dropped(void);
#endif
