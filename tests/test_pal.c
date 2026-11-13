#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pal.h"
#include "matrix.h"

static uint64_t clock_us;
static uint32_t shape, all_shapes[16], known_shapes[4], random_state = 6543;
static uint8_t corners, whites, known_positions[4], seen_queries;
static unsigned total_shapes;
static bool usb;
static char screen[8][32];
uint64_t time_us_64(void) { return clock_us; }
bool stdio_usb_connected(void) { return usb; }
uint32_t get_rand_32(void) { random_state = random_state * 1664525u + 1013904223u; return random_state; }
void npClear(void) { shape = corners = whites = 0; }
void npWrite(void) { clock_us += 1000; }
uint64_t npWriteTimed(void) { npWrite(); return time_us_64(); }
void npSetPixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b) {
    assert(x <= 4 && y <= 4);
    if (x >= 1 && x <= 3 && y >= 1 && y <= 3) {
        assert(!r && g == 20 && !b);
        shape |= 1u << (y * 5 + x);
    } else {
        assert((x == 0 || x == 4) && (y == 0 || y == 4) && b == 20);
        uint8_t bit = 1u << ((y / 4) * 2 + x / 4);
        corners |= bit;
        if (r || g) { assert(r == 20 && g == 20); whites |= bit; }
    }
}
void display_show_lines(const char *lines[], size_t n) {
    assert(n <= 8);
    memset(screen, 0, sizeof(screen));
    for (size_t i = 0; i < n; ++i) { assert(strlen(lines[i]) <= 15); strcpy(screen[i], lines[i]); }
    clock_us += 25000;
}
static void step(control_event_t event, uint64_t elapsed) {
    clock_us += elapsed;
    assert(!pal_update(event, clock_us));
    pal_serial_update();
}
static unsigned known_index(unsigned count) {
    for (unsigned i = 0; i < count; ++i) if (known_shapes[i] == shape) return i;
    assert(false); return 0;
}
static void encode(unsigned count, bool retry) {
    uint8_t used_positions = 0, used_shapes = 0;
    for (unsigned i = 0; i < count; ++i) {
        assert(pal_get_state() == PAL_ENCODING && shape && !whites);
        assert(__builtin_popcount(corners) == 1);
        uint8_t position = __builtin_ctz(corners);
        assert(!(used_positions & corners)); used_positions |= corners;
        if (retry) {
            unsigned index = known_index(count);
            assert(!(used_shapes & (1u << index)));
            used_shapes |= 1u << index;
            assert(known_positions[index] == position); // Mesma associação ao reapresentar.
        } else {
            for (unsigned j = 0; j < total_shapes; ++j) assert(all_shapes[j] != shape);
            assert(total_shapes < 16); all_shapes[total_shapes++] = shape;
            known_shapes[i] = shape; known_positions[i] = position;
        }
        step(CONTROL_CONFIRM, PAL_ENCODING_US - 1);
        assert(pal_get_state() == PAL_ENCODING);
        step(CONTROL_NONE, 1); assert(!shape && !corners && pal_get_state() == PAL_GAP);
        step(CONTROL_NONE, PAL_GAP_US);
    }
    assert(pal_get_state() == PAL_RETENTION);
    step(CONTROL_NONE, PAL_RETENTION_US - 1); assert(!shape);
    step(CONTROL_NONE, 1); assert(pal_get_state() == PAL_CHOICE);
    seen_queries = 0;
}
static void respond(unsigned count, pal_outcome_t outcome) {
    assert(pal_get_state() == PAL_CHOICE && corners == 15 && whites == 1);
    unsigned index = known_index(count);
    assert(!(seen_queries & (1u << index))); seen_queries |= 1u << index;
    uint64_t onset = clock_us;
    uint8_t target = known_positions[index];
    if (outcome == PAL_OMISSION) {
        step(CONTROL_RIGHT, 1000);
        clock_us = onset + PAL_TIMEOUT_US;
        step(CONTROL_CONFIRM, 0); // O prazo vence mesmo com confirmação.
    } else {
        uint8_t answer = outcome == PAL_WRONG ? target ^ 1 : target;
        step(CONTROL_UP, 1000); step(CONTROL_LEFT, 1000); assert(whites == 1);
        if (answer & 1) step(CONTROL_RIGHT, 1000);
        if (answer & 2) step(CONTROL_DOWN, 1000);
        assert(whites == (1u << answer) && corners == 15);
        uint64_t elapsed = clock_us + 100000 - onset;
        step(CONTROL_CONFIRM, 100000);
        assert(pal_result()->records[pal_result()->record_count - 1].elapsed_us == elapsed);
    }
    const pal_record_t *r = &pal_result()->records[pal_result()->record_count - 1];
    assert(r->target_position == target && r->outcome == outcome && r->onset_us == onset);
    if (outcome == PAL_OMISSION) assert(r->selected_position == PAL_NO_POSITION);
    assert(pal_get_state() == PAL_INTERVAL && !shape && !corners);
    step(CONTROL_CONFIRM, PAL_INTERVAL_US - 1); assert(pal_get_state() == PAL_INTERVAL);
    step(CONTROL_NONE, 1);
}
static void train(void) {
    total_shapes = 0;
    pal_begin(); step(CONTROL_NONE, 9000000); assert(pal_get_state() == PAL_INSTRUCTIONS);
    step(CONTROL_CONFIRM, 0); encode(2, false);
    respond(2, PAL_CORRECT); respond(2, PAL_CORRECT);
    assert(pal_get_state() == PAL_READY && pal_result()->correct == 0 && pal_result()->rounds_started == 0);
    step(CONTROL_NONE, 9000000); assert(pal_get_state() == PAL_READY);
}
int main(void) {
    train();
    // Caminho mais longo permitido: três rodadas em cada nível, total de 29 registros.
    for (unsigned count = 2; count <= 4; ++count) {
        step(CONTROL_CONFIRM, 0);
        for (unsigned round = 1; round <= PAL_MAX_ROUNDS; ++round) {
            encode(count, round > 1);
            for (unsigned trial = 0; trial < count; ++trial) {
                pal_outcome_t outcome = PAL_CORRECT;
                if (!trial && round < 3) outcome = count == 3 && round == 2 ? PAL_OMISSION : PAL_WRONG;
                respond(count, outcome);
                const pal_record_t *r = &pal_result()->records[pal_result()->record_count - 1];
                assert(r->pairs == count && r->round == round && r->trial == trial + 1 && !r->training);
            }
            assert(seen_queries == (1u << count) - 1);
            if (round < 3) {
                assert(pal_get_state() == PAL_RETRY);
                step(CONTROL_NONE, 9000000); assert(pal_get_state() == PAL_RETRY);
                step(CONTROL_CONFIRM, 0);
            }
        }
        assert(pal_result()->levels_completed == count - 1);
        assert(pal_get_state() == (count < 4 ? PAL_NEXT_LEVEL : PAL_RESULTS));
    }
    assert(pal_result()->record_count == PAL_MAX_RECORDS);
    assert(pal_result()->rounds_started == 9 && pal_result()->correct == 21);
    assert(pal_result()->wrong == 5 && pal_result()->omissions == 1);
    assert(!pal_result()->interrupted && !pal_result()->limit_reached && !shape && !corners);
    assert(strcmp(screen[2], "NIVEIS 3 DE 3") == 0);
    usb = true; pal_serial_update(); pal_serial_update();
    assert(pal_update(CONTROL_BACK, clock_us));
    usb = false; pal_serial_update();
    // Limite: três rodadas malsucedidas encerram sem liberar nível seguinte.
    train(); step(CONTROL_CONFIRM, 0);
    for (unsigned round = 1; round <= 3; ++round) {
        encode(2, round > 1); respond(2, PAL_WRONG); respond(2, PAL_OMISSION);
        if (round < 3) { assert(pal_get_state() == PAL_RETRY); step(CONTROL_CONFIRM, 0); }
    }
    assert(pal_get_state() == PAL_RESULTS && pal_result()->limit_reached);
    assert(pal_result()->levels_completed == 0 && pal_result()->record_count == 8);
    // Treino não contamina métricas; repetir preserva associações e libera só após acertos.
    total_shapes = 0; pal_begin(); step(CONTROL_CONFIRM, 0); encode(2, false);
    respond(2, PAL_WRONG); respond(2, PAL_CORRECT);
    assert(pal_get_state() == PAL_TRAINING_FAILED && pal_result()->wrong == 0);
    step(CONTROL_CONFIRM, 0); assert(pal_result()->record_count == 0);
    encode(2, true); respond(2, PAL_CORRECT); respond(2, PAL_CORRECT);
    assert(pal_get_state() == PAL_READY);
    step(CONTROL_CONFIRM, 0); encode(2, false);
    step(CONTROL_BACK, 0);
    assert(pal_result()->record_count == 3 && pal_result()->records[2].outcome == PAL_ABORTED);
    assert(pal_result()->interrupted && !shape && !corners);
    // Aborto em cada etapa anterior à escolha não inventa uma resposta.
    for (unsigned phase = 0; phase < 4; ++phase) {
        pal_begin();
        if (phase) step(CONTROL_CONFIRM, 0);
        if (phase >= 2) step(CONTROL_NONE, PAL_ENCODING_US);
        if (phase == 3) {
            step(CONTROL_NONE, PAL_GAP_US);
            step(CONTROL_NONE, PAL_ENCODING_US);
            step(CONTROL_NONE, PAL_GAP_US);
            assert(pal_get_state() == PAL_RETENTION);
        }
        step(CONTROL_BACK, 0);
        assert(pal_result()->interrupted && pal_result()->record_count == 0 && !shape && !corners);
    }
    // Novos sorteios, todos os níveis sem repetição (11 registros, três rodadas).
    for (unsigned run = 0; run < 16; ++run) {
        train();
        for (unsigned count = 2; count <= 4; ++count) {
            step(CONTROL_CONFIRM, 0); encode(count, false);
            for (unsigned i = 0; i < count; ++i) respond(count, PAL_CORRECT);
        }
        assert(pal_result()->record_count == 11 && pal_result()->correct == 9);
        assert(pal_result()->rounds_started == 3 && pal_get_state() == PAL_RESULTS);
    }
    total_shapes = 0; pal_begin(); step(CONTROL_CONFIRM, 0); encode(2, false);
    uint64_t onset = clock_us;
    assert(!pal_update_captured(CONTROL_RIGHT, onset - 1, clock_us));
    assert(whites == 1);
    uint8_t position = known_positions[known_index(2)];
    if (position & 1) step(CONTROL_RIGHT, 1000);
    if (position & 2) step(CONTROL_DOWN, 1000);
    uint64_t captured = clock_us + 100000;
    clock_us = onset + PAL_TIMEOUT_US + 100000;
    assert(!pal_update_captured(CONTROL_CONFIRM, captured, clock_us));
    assert(pal_result()->records[0].outcome == PAL_CORRECT);
    assert(pal_result()->records[0].elapsed_us == captured - onset);
    puts("PASS PAL: associacoes, niveis, treino, repeticoes, limite, omissoes e interrupcao");
    return 0;
}
