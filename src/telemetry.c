#include "telemetry.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#ifdef TREMO_FREERTOS_ENABLED
#include "FreeRTOS.h"
#include "semphr.h"
static SemaphoreHandle_t queue_mutex;
static void lock_queue(void) {
    BaseType_t taken = xSemaphoreTake(queue_mutex, portMAX_DELAY);
    configASSERT(taken == pdTRUE);
}
static void unlock_queue(void) { xSemaphoreGive(queue_mutex); }
#else
static void lock_queue(void) {}
static void unlock_queue(void) {}
#endif
static char messages[TELEMETRY_CAPACITY][TELEMETRY_PAYLOAD];
static char device_id[17];
static unsigned head, count;
static uint32_t sequence, dropped, run;
static uint64_t current_id;
static bool enabled;
void telemetry_init(const char *device, bool on) {
#ifdef TREMO_FREERTOS_ENABLED
    if (!queue_mutex) queue_mutex = xSemaphoreCreateMutex();
    configASSERT(queue_mutex);
#endif
    lock_queue();
    head = count = sequence = dropped = 0; current_id = 0; enabled = on;
    snprintf(device_id, sizeof(device_id), "%s", device);
    unlock_queue();
}
bool telemetry_enabled(void) {
    lock_queue(); bool value = enabled; unlock_queue(); return value;
}
unsigned telemetry_pending(void) {
    lock_queue(); unsigned value = count; unlock_queue(); return value;
}
uint32_t telemetry_dropped(void) {
    lock_queue(); uint32_t value = dropped; unlock_queue(); return value;
}
const char *telemetry_peek(void) {
    lock_queue(); const char *value = count ? messages[head] : NULL; unlock_queue(); return value;
}
bool telemetry_copy(char *payload, size_t capacity) {
    lock_queue();
    bool available = count && payload && capacity > strlen(messages[head]);
    if (available) strcpy(payload, messages[head]);
    unlock_queue();
    return available;
}
void telemetry_ack(void) {
    lock_queue();
    if (count) { head = (head + 1) % TELEMETRY_CAPACITY; --count; }
    unlock_queue();
}
static void emit_locked(uint64_t session_id, uint64_t time_us, const char *type, bool essential, const char *format, va_list args) {
    if (!enabled || !session_id) return;
    // Eventos atrasados de outra sessão não devem receber a sequência atual.
    if (session_id != current_id) return;
    uint32_t seq = sequence++;
    if (count == TELEMETRY_CAPACITY || (!essential && count >= 16)) { ++dropped; return; }
    char *out = messages[(head + count) % TELEMETRY_CAPACITY];
    int prefix = snprintf(out, TELEMETRY_PAYLOAD,
        "{\"v\":1,\"device\":\"%s\",\"session\":\"%016llx\",\"seq\":%lu,\"run\":%lu,\"type\":\"%s\",\"t_us\":%llu,\"data\":",
        device_id, (unsigned long long)session_id, (unsigned long)seq, (unsigned long)run, type, (unsigned long long)time_us);
    if (prefix < 0 || prefix >= TELEMETRY_PAYLOAD - 2) { ++dropped; return; }
    int n = vsnprintf(out + prefix, TELEMETRY_PAYLOAD - prefix - 1, format, args);
    if (n < 0 || n >= TELEMETRY_PAYLOAD - prefix - 1) { ++dropped; return; }
    out[prefix + n] = '}'; out[prefix + n + 1] = 0; ++count;
}
void telemetry_emit(uint64_t session_id, uint64_t time_us, const char *type, bool essential, const char *format, ...) {
    va_list args;
    va_start(args, format);
    lock_queue();
    emit_locked(session_id, time_us, type, essential, format, args);
    unlock_queue();
    va_end(args);
}
void telemetry_next_run(void) {
    lock_queue(); ++run; unlock_queue();
}
void telemetry_start(const session_t *s) {
    lock_queue();
    current_id = s->id; run = 0; sequence = dropped = 0;
    unlock_queue();
    telemetry_emit(s->id, s->started_us, "start", true,
        "{\"patient_id\":\"%s\",\"kind\":%u,\"duration_ms\":%lu,\"sample_ms\":%u,\"tests_mask\":%u,\"tremor_first\":%s,\"cognitive_mpu\":%s,\"protocol\":\"tremo-adaptado-v1\"}",
        s->patient_id, s->kind, (unsigned long)s->config.duration_ms, s->config.sample_ms, s->config.tests_mask, s->config.tremor_first ? "true" : "false", s->config.cognitive_mpu ? "true" : "false");
}
void telemetry_state(const session_t *s, uint64_t now_us, bool terminal) {
    telemetry_emit(s->id, now_us, terminal ? "end" : "state", true,
        "{\"state\":%u,\"active_us\":%llu,\"paused_us\":%llu,\"pauses\":%lu,\"tx_dropped\":%lu}",
        s->state, (unsigned long long)session_active_us(now_us), (unsigned long long)s->paused_us,
        (unsigned long)s->pauses, (unsigned long)telemetry_dropped());
}
