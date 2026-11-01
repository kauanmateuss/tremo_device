#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "prm.h"
#include "patterns.h"
#include "matrix.h"

static uint64_t clock_us;
static uint32_t pixels, sample[PRM_TEST_TRIALS], training_patterns[4];
static uint32_t random_state = 1234;
static bool usb;
static char screen[8][32];
uint64_t time_us_64(void) { return clock_us; }
bool stdio_usb_connected(void) { return usb; }
uint32_t get_rand_32(void) { random_state = random_state * 1664525u + 1013904223u; return random_state; }
void npClear(void) { pixels = 0; }
void npWrite(void) { clock_us += 1000; }
void npSetPixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b) {
    assert(x >= 1 && x <= 3 && y >= 1 && y <= 3 && !r && g == 20 && !b);
    pixels |= 1u << (y * 5 + x);
}
void display_show_lines(const char *lines[], size_t n) {
    assert(n <= 8);
    memset(screen, 0, sizeof(screen));
    for (size_t i = 0; i < n; ++i) { assert(strlen(lines[i]) <= 15); strcpy(screen[i], lines[i]); }
    clock_us += 25000;
}
static void step(control_event_t event, uint64_t elapsed) {
    clock_us += elapsed;
    assert(!prm_update(event, clock_us));
}
static void encode(unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        assert(prm_get_state() == PRM_ENCODING && pixels);
        sample[i] = pixels;
        for (unsigned j = 0; j < i; ++j) assert(sample[j] != sample[i]);
        step(CONTROL_CONFIRM, PRM_ENCODING_US - 1); // A não pula exposição.
        assert(prm_get_state() == PRM_ENCODING);
        step(CONTROL_NONE, 1); assert(!pixels && prm_get_state() == PRM_GAP);
        step(CONTROL_NONE, PRM_GAP_US);
    }
    assert(prm_get_state() == PRM_RETENTION);
    step(CONTROL_NONE, PRM_RETENTION_US - 1); assert(!pixels);
    step(CONTROL_NONE, 1); assert(prm_get_state() == PRM_CHOICE);
}
static unsigned choose(unsigned index, unsigned count, bool wrong) {
    assert(prm_get_state() == PRM_CHOICE);
    uint64_t onset = clock_us;
    uint32_t first = pixels;
    step(CONTROL_CONFIRM, 1000); // Não aceita sem ver ambas.
    assert(prm_get_state() == PRM_CHOICE);
    step(CONTROL_RIGHT, 1000);
    uint32_t second = pixels;
    assert(first != second);
    unsigned correct = second == sample[count - 1 - index];
    assert((correct ? second : first) == sample[count - 1 - index]);
    uint32_t novel = correct ? first : second;
    for (unsigned i = 0; i < count; ++i) assert(novel != sample[i]);
    if (count == PRM_TRAINING_TRIALS) training_patterns[2 + index] = novel;
    else for (unsigned i = 0; i < 4; ++i) assert(novel != training_patterns[i]);
    unsigned answer = correct ^ wrong;
    if (!answer) step(CONTROL_LEFT, 1000);
    uint64_t expected = clock_us + 100000 - onset;
    step(CONTROL_CONFIRM, 100000);
    const prm_result_t *r = prm_result();
    const prm_record_t *last = &r->records[r->record_count - 1];
    assert(last->outcome == (wrong ? PRM_WRONG : PRM_CORRECT));
    assert(last->elapsed_us == expected && last->onset_us == onset);
    assert(!pixels && prm_get_state() == PRM_INTERVAL);
    step(CONTROL_CONFIRM, PRM_INTERVAL_US - 1); assert(prm_get_state() == PRM_INTERVAL);
    step(CONTROL_NONE, 1);
    return correct;
}
static void training(void) {
    encode(2);
    training_patterns[0] = sample[0]; training_patterns[1] = sample[1];
    choose(0, 2, false); choose(1, 2, false);
    assert(prm_get_state() == PRM_READY && prm_result()->correct == 0);
    step(CONTROL_NONE, 9000000); assert(prm_get_state() == PRM_READY);
}
int main(void) {
    // Banco com IDs estáveis, padrões distintos e luminosidade equivalente (5 LEDs).
    uint32_t seen[PATTERN_COUNT];
    for (uint8_t i = 0; i < PATTERN_COUNT; ++i) {
        patterns_show(i); seen[i] = pixels;
        assert(__builtin_popcount(pixels) == 5);
        for (uint8_t j = 0; j < i; ++j) assert(seen[j] != seen[i]);
    }
    patterns_show(PATTERN_COUNT); assert(!pixels);
    prm_begin(); step(CONTROL_NONE, 9000000); assert(prm_get_state() == PRM_INSTRUCTIONS);
    step(CONTROL_CONFIRM, 0); training(); step(CONTROL_CONFIRM, 0);
    encode(6);
    for (unsigned i = 0; i < 6; ++i)
        for (unsigned j = 0; j < 4; ++j) assert(sample[i] != training_patterns[j]);
    unsigned correct_right = 0;
    for (unsigned i = 0; i < 5; ++i) correct_right += choose(i, 6, i == 1);
    // Prazo tem prioridade sobre A; navegar não reinicia o relógio.
    uint64_t last_onset = clock_us;
    step(CONTROL_RIGHT, 1000);
    clock_us = last_onset + PRM_TIMEOUT_US;
    step(CONTROL_CONFIRM, 0);
    assert(prm_result()->records[7].outcome == PRM_OMISSION);
    correct_right += prm_result()->records[7].target_option;
    step(CONTROL_NONE, PRM_INTERVAL_US);
    assert(correct_right == 3 && prm_get_state() == PRM_RESULTS && !pixels);
    assert(prm_result()->correct == 4 && prm_result()->wrong == 1 && prm_result()->omissions == 1);
    assert(strcmp(screen[5], "ACURACIA 66 PC") == 0);
    usb = true; prm_serial_update(); prm_serial_update();
    assert(prm_update(CONTROL_BACK, clock_us));
    usb = false; prm_serial_update();
    // Treino malsucedido repete e limpa registros; avaliação não começa sozinha.
    prm_begin(); step(CONTROL_CONFIRM, 0); encode(2);
    choose(0, 2, true); choose(1, 2, false);
    assert(prm_get_state() == PRM_TRAINING_FAILED);
    step(CONTROL_CONFIRM, 0); assert(prm_result()->record_count == 0);
    training(); step(CONTROL_CONFIRM, 0); encode(6);
    step(CONTROL_BACK, 0); assert(prm_result()->interrupted);
    assert(prm_result()->records[2].outcome == PRM_ABORTED && !pixels);
    prm_begin(); step(CONTROL_CONFIRM, 0); step(CONTROL_BACK, 0);
    assert(prm_result()->record_count == 0 && prm_result()->interrupted);
    // Novas sessões continuam respeitando limite fixo e treino/avaliação separados.
    for (unsigned run = 0; run < 16; ++run) {
        prm_begin(); step(CONTROL_CONFIRM, 0); training();
        step(CONTROL_CONFIRM, 0); encode(6);
        for (unsigned i = 0; i < 6; ++i) choose(i, 6, false);
        assert(prm_result()->record_count == PRM_MAX_RECORDS && prm_result()->correct == 6);
    }
    puts("PASS PRM: banco, memorizacao, alternativas, treino, prazos, resultados e interrupcao");
    return 0;
}
