#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "telemetry.h"

static void test_payload_limits(void) {
    telemetry_init("0123456789abcdef", true);
    session_init(12);
    session_begin(SESSION_MOT, 100);
    telemetry_ack();

    // Cada linha MPU tem um buffer de 256 bytes. Exercita até sua capacidade,
    // maior que as leituras reais, com o timestamp de maior largura possível.
    char row[256];
    row[0] = '[';
    for (unsigned i = 1; i < 254; ++i) row[i] = i % 2 ? '0' : ',';
    row[254] = ']';
    row[255] = '\0';
    telemetry_emit(session_current()->id, UINT64_MAX, "samples", false,
        "{\"module\":\"mpu\",\"rows\":[%s,%s]}", row, row);
    assert(telemetry_pending() == 1 && telemetry_dropped() == 0);
    // A sequência atual tem um dígito; UINT32_MAX tem dez.
    assert(strlen(telemetry_peek()) + 9 + 1 <= TELEMETRY_PAYLOAD);
    char copy[TELEMETRY_PAYLOAD];
    assert(telemetry_copy(copy, sizeof(copy)));
    assert(strcmp(copy, telemetry_peek()) == 0);
    assert(!telemetry_copy(copy, strlen(telemetry_peek())));
    telemetry_ack();

    // Aceita exatamente o limite, mas rejeita um byte a mais sem enfileirar
    // JSON parcial. O envelope permanece igual nas próximas duas emissões.
    telemetry_emit(session_current()->id, 100, "trial", true, "%s", "");
    size_t envelope = strlen(telemetry_peek());
    telemetry_ack();
    char body[TELEMETRY_PAYLOAD];
    size_t length = TELEMETRY_PAYLOAD - envelope - 1;
    memset(body, ' ', length + 1);
    body[0] = '0'; // Número JSON seguido por espaços.
    body[length] = '\0';
    telemetry_emit(session_current()->id, 100, "trial", true, "%s", body);
    assert(telemetry_pending() == 1 && strlen(telemetry_peek()) == TELEMETRY_PAYLOAD - 1);
    telemetry_ack();
    body[length] = ' ';
    body[length + 1] = '\0';
    telemetry_emit(session_current()->id, 100, "trial", true, "%s", body);
    assert(telemetry_pending() == 0 && telemetry_dropped() == 1);
}

int main(void) {
    test_payload_limits();
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
