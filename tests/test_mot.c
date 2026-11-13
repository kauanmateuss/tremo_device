#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mot.h"
#include "matrix.h"

static uint64_t clock_us;
static bool usb;
static unsigned int tx, ty, cx, cy, lit;
static char screen[8][32];
uint64_t time_us_64(void) { return clock_us; }
bool stdio_usb_connected(void) { return usb; }
void display_show_lines(const char *lines[], size_t n) {
    memset(screen, 0, sizeof(screen));
    assert(n <= 8);
    for (size_t i = 0; i < n; ++i) {
        assert(strlen(lines[i]) <= 15);
        strcpy(screen[i], lines[i]);
    }
    clock_us += 25000; // OLED lento: não pode entrar na latência do primeiro estímulo.
}
void npClear(void) { lit = 0; }
void npWrite(void) { clock_us += 1000; }
uint64_t npWriteTimed(void) { npWrite(); return time_us_64(); }
void npSetPixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b) {
    assert(x < 5 && y < 5 && r == 0);
    ++lit;
    if (b) { cx = x; cy = y; }
    else { assert(g > 0); tx = x; ty = y; }
}
static void step(control_event_t event, uint64_t elapsed) {
    clock_us += elapsed;
    assert(!mot_update(event, clock_us));
}
static void hit_target(void) {
    while (cx > tx) step(CONTROL_LEFT, 100000);
    while (cx < tx) step(CONTROL_RIGHT, 100000);
    while (cy > ty) step(CONTROL_UP, 100000);
    while (cy < ty) step(CONTROL_DOWN, 100000);
    step(CONTROL_CONFIRM, 100000);
}
static void pass_training(void) {
    for (int i = 0; i < MOT_TRAINING_TRIALS; ++i) {
        assert(mot_get_state() == MOT_STIMULUS);
        hit_target();
        assert(mot_get_state() == MOT_INTERVAL);
        step(CONTROL_NONE, MOT_INTERVAL_US);
    }
    assert(mot_get_state() == MOT_READY);
    assert(mot_result()->record_count == MOT_TRAINING_TRIALS);
    assert(mot_result()->correct == 0); // Treino não contamina a avaliação.
}
int main(void) {
    mot_begin(); assert(mot_get_state() == MOT_INSTRUCTIONS && lit == 0);
    step(CONTROL_NONE, 5000000); assert(mot_result()->record_count == 0);
    uint64_t before = clock_us;
    step(CONTROL_CONFIRM, 0);
    assert(cx == 2 && cy == 2 && tx == 1 && ty == 2);
    uint64_t onset = clock_us;
    assert(onset == before + 26000);
    pass_training();
    assert(mot_result()->records[0].onset_us == onset);
    assert(mot_result()->records[0].elapsed_us == 201000); // Só movimento+envio LED+confirmação.
    step(CONTROL_NONE, 30000000); assert(mot_get_state() == MOT_READY);
    step(CONTROL_CONFIRM, 0);
    uint64_t sum_correct = 0;
    for (int i = 0; i < MOT_TEST_TRIALS; ++i) {
        assert(mot_get_state() == MOT_STIMULUS);
        assert(cx == 2 && cy == 2 && (tx != 2 || ty != 2));
        if (i == 1) step(CONTROL_CONFIRM, 100000); // Seleção fora do alvo.
        else if (i == 2) step(CONTROL_CONFIRM, MOT_TIMEOUT_US); // No prazo exato: omissão.
        else hit_target();
        const mot_record_t *r = &mot_result()->records[MOT_TRAINING_TRIALS + i];
        if (r->outcome == MOT_CORRECT) sum_correct += r->elapsed_us;
        assert(!r->training);
        assert(lit == 0);
        step(CONTROL_CONFIRM, 500000); // A durante intervalo não pula etapa.
        assert(mot_get_state() == MOT_INTERVAL);
        step(CONTROL_NONE, 500000);
    }
    assert(mot_get_state() == MOT_RESULTS);
    assert(mot_result()->record_count == MOT_MAX_RECORDS);
    assert(mot_result()->correct == 8 && mot_result()->wrong == 1 && mot_result()->omissions == 1);
    assert(mot_result()->correct_time_us == sum_correct);
    assert(!mot_result()->interrupted);
    assert(strcmp(screen[5], "ACURACIA 80 PC") == 0);
    usb = true; mot_serial_update(); mot_serial_update(); // Uma exportação por conexão.
    assert(mot_update(CONTROL_BACK, clock_us));
    usb = false; mot_serial_update();
    mot_begin(); step(CONTROL_CONFIRM, 0);
    // Bordas da matriz: movimentos extras não podem dar a volta.
    for (int i = 0; i < 7; ++i) step(CONTROL_UP, 10000);
    for (int i = 0; i < 7; ++i) step(CONTROL_LEFT, 10000);
    assert(cx == 0 && cy == 0);
    step(CONTROL_CONFIRM, 100000); step(CONTROL_NONE, MOT_INTERVAL_US);
    for (int i = 1; i < 3; ++i) {
        step(CONTROL_NONE, MOT_TIMEOUT_US); step(CONTROL_NONE, MOT_INTERVAL_US);
    }
    assert(mot_get_state() == MOT_TRAINING_FAILED);
    step(CONTROL_CONFIRM, 0); assert(mot_result()->record_count == 0);
    pass_training();
    step(CONTROL_CONFIRM, 0);
    step(CONTROL_BACK, 500000);
    assert(mot_get_state() == MOT_RESULTS && mot_result()->interrupted && lit == 0);
    assert(mot_result()->records[3].outcome == MOT_ABORTED);
    assert(mot_result()->correct + mot_result()->wrong + mot_result()->omissions == 0);
    assert(strcmp(screen[5], "SEM DADOS") == 0);
    mot_begin(); step(CONTROL_BACK, 0);
    assert(mot_result()->record_count == 0 && mot_result()->interrupted);
    // Uma confirmação enfileirada antes do prazo deve manter sua latência.
    mot_begin(); step(CONTROL_CONFIRM, 0);
    onset = clock_us;
    assert(!mot_update_captured(CONTROL_LEFT, onset - 1, clock_us));
    assert(cx == 2); // Movimento anterior ao alvo foi descartado.
    step(CONTROL_LEFT, 100000);
    uint64_t captured = clock_us + 100000;
    clock_us = onset + MOT_TIMEOUT_US + 50000;
    assert(!mot_update_captured(CONTROL_CONFIRM, captured, clock_us));
    assert(mot_result()->records[0].outcome == MOT_CORRECT);
    assert(mot_result()->records[0].elapsed_us == captured - onset);
    puts("PASS MOT: treino, inicio, tempo, limites, acuracia, omissao, interrupcao e exportacao");
}
