#include "telemetry.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
static char messages[TELEMETRY_CAPACITY][TELEMETRY_PAYLOAD];
static char device_id[17];
static unsigned head, count;
static uint32_t sequence, dropped;
static uint64_t current_id;
static bool enabled;
void telemetry_init(const char *device, bool on) {
    head = count = sequence = dropped = 0; current_id = 0; enabled = on;
    snprintf(device_id, sizeof(device_id), "%s", device);
}
bool telemetry_enabled(void) { return enabled; }
unsigned telemetry_pending(void) { return count; }
uint32_t telemetry_dropped(void) { return dropped; }
const char *telemetry_peek(void) { return count ? messages[head] : NULL; }
void telemetry_ack(void) { if (count) { head = (head + 1) % TELEMETRY_CAPACITY; --count; } }
void telemetry_emit(uint64_t session_id, uint64_t time_us, const char *type, bool essential, const char *format, ...) {
    if (!enabled || !session_id) return;
    // Eventos atrasados de outra sessão não devem receber a sequência atual.
    if (session_id != current_id) return;
    uint32_t seq = sequence++;
    if (count == TELEMETRY_CAPACITY || (!essential && count >= 16)) { ++dropped; return; }
    char *out = messages[(head + count) % TELEMETRY_CAPACITY];
    int prefix = snprintf(out, TELEMETRY_PAYLOAD,
        "{\"v\":1,\"device\":\"%s\",\"session\":\"%016llx\",\"seq\":%lu,\"type\":\"%s\",\"t_us\":%llu,\"data\":",
        device_id, (unsigned long long)session_id, (unsigned long)seq, type, (unsigned long long)time_us);
    if (prefix < 0 || prefix >= TELEMETRY_PAYLOAD - 2) { ++dropped; return; }
    va_list args; va_start(args, format);
    int n = vsnprintf(out + prefix, TELEMETRY_PAYLOAD - prefix - 1, format, args);
    va_end(args);
    if (n < 0 || n >= TELEMETRY_PAYLOAD - prefix - 1) { ++dropped; return; }
    out[prefix + n] = '}'; out[prefix + n + 1] = 0; ++count;
}
void telemetry_start(const session_t *s) {
    current_id = s->id; sequence = dropped = 0;
    telemetry_emit(s->id, s->started_us, "start", true,
        "{\"kind\":%u,\"duration_ms\":%lu,\"sample_ms\":%u,\"tests_mask\":%u,\"tremor_first\":%s,\"protocol\":\"tremo-adaptado-v1\"}",
        s->kind, (unsigned long)s->config.duration_ms, s->config.sample_ms, s->config.tests_mask, s->config.tremor_first ? "true" : "false");
}
void telemetry_state(const session_t *s, uint64_t now_us, bool terminal) {
    telemetry_emit(s->id, now_us, terminal ? "end" : "state", true,
        "{\"state\":%u,\"active_us\":%llu,\"paused_us\":%llu,\"pauses\":%lu,\"tx_dropped\":%lu}",
        s->state, (unsigned long long)session_active_us(now_us), (unsigned long long)s->paused_us,
        (unsigned long)s->pauses, (unsigned long)dropped);
}
