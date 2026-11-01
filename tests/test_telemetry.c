#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "telemetry.h"
int main(void) {
    telemetry_init("0123456789abcdef", true); session_init(12); session_begin(SESSION_MOT, 100);
    assert(telemetry_pending() == 1 && strstr(telemetry_peek(), "\"seq\":0"));
    char first[TELEMETRY_PAYLOAD]; strcpy(first, telemetry_peek());
    assert(strcmp(first, telemetry_peek()) == 0); // Sem ACK/reconexão: mesma identidade e conteúdo.
    telemetry_ack(); assert(!telemetry_pending());
    for (unsigned i = 0; i < 20; ++i) telemetry_emit(session_current()->id, 200, "samples", false, "{\"rows\":[]}");
    assert(telemetry_pending() == 16 && telemetry_dropped() == 4);
    for (unsigned i = 0; i < 70; ++i) telemetry_emit(session_current()->id, 300, "trial", true, "{}");
    assert(telemetry_pending() == 86); // Resultados continuam tendo espaço com a fila de MPU cheia.
    session_finish(SESSION_COMPLETE, 400); assert(telemetry_pending() == 87);
    while (telemetry_pending()) { puts(telemetry_peek()); telemetry_ack(); }
    session_begin(SESSION_RTI, 500); puts(telemetry_peek()); telemetry_ack();
    char huge[1000]; memset(huge, 'a', sizeof(huge) - 1); huge[999] = 0;
    telemetry_emit(session_current()->id, 600, "trial", true, "{\"value\":\"%s\"}", huge);
    assert(!telemetry_pending() && telemetry_dropped() == 1);
    for (unsigned i = 0; i < 100; ++i) telemetry_emit(session_current()->id, 700, "state", true, "{}");
    assert(telemetry_pending() == TELEMETRY_CAPACITY && telemetry_dropped() == 5);
    telemetry_init("0123456789abcdef", false); session_finish(SESSION_INTERRUPTED, 900);
    assert(!telemetry_pending());
    puts("PASS telemetria: sequencias, reenvio, prioridade, lotacao e truncamento");
}
