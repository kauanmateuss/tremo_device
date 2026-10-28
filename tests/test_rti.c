#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rti.h"
#include "matrix.h"

static uint64_t clock_us;
static bool usb, lit;
static unsigned int oled_writes, rand_calls;
static uint32_t planned_wait;
static char screen[8][32];
static control_button_state_t button = {.settled = true};
uint64_t time_us_64(void) { return clock_us; }
bool stdio_usb_connected(void) { return usb; }
uint32_t get_rand_32(void) {
    uint32_t value = (rand_calls++ % 2) ? 2000 : 0;
    planned_wait = RTI_MIN_WAIT_US + value * 1000;
    return value;
}
void display_show_lines(const char *lines[], size_t n) {
    assert(n <= 8);
    ++oled_writes;
    memset(screen, 0, sizeof(screen));
    for (size_t i = 0; i < n; ++i) {
        assert(strlen(lines[i]) <= 15);
        strcpy(screen[i], lines[i]);
    }
    clock_us += 25000;
}
void npClear(void) { lit = false; }
void npWrite(void) { clock_us += 1000; }
void npSetPixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b) {
    assert(x == 2 && y == 2 && r == 0 && g == 20 && b == 0);
    lit = true;
}
static void step(control_event_t event, uint64_t elapsed) {
    clock_us += elapsed;
    assert(!rti_update(event, button, clock_us));
}
static void press(void) {
    clock_us += 100000;
    button = (control_button_state_t){.pressed = true, .settled = true, .edge_us = clock_us - 30000};
    step(CONTROL_CONFIRM, 0);
}
static void arm_trial(void) {
    assert(rti_get_state() == RTI_WAIT_RELEASE);
    // Botão mantido após iniciar ou após omissão não arma automaticamente.
    button.pressed = true;
    step(CONTROL_NONE, 100000);
    assert(rti_get_state() == RTI_WAIT_RELEASE);
    button.pressed = false;
    step(CONTROL_CONFIRM_RELEASE, 100000);
    assert(rti_get_state() == RTI_WAIT_HOLD);
    step(CONTROL_NONE, 100000); assert(rti_get_state() == RTI_WAIT_HOLD);
    press();
    assert(rti_get_state() == RTI_FOREPERIOD && !lit);
}
static uint64_t present(void) {
    unsigned int before = oled_writes;
    step(CONTROL_NONE, planned_wait - 1);
    assert(rti_get_state() == RTI_FOREPERIOD && !lit);
    step(CONTROL_NONE, 1);
    assert(rti_get_state() == RTI_STIMULUS && lit);
    assert(oled_writes == before); // OLED fora da apresentação cronometrada.
    return clock_us;
}
static void respond(uint64_t latency) {
    // Evento entregue 30 ms depois da borda; RT deve usar a borda, não a entrega.
    clock_us += latency + 30000;
    button = (control_button_state_t){.pressed = false, .settled = true, .edge_us = clock_us - 30000};
    step(CONTROL_CONFIRM_RELEASE, 0);
    assert(rti_get_state() == RTI_INTERVAL && !lit);
}
static void pass_training(void) {
    for (int i = 0; i < RTI_TRAINING_TRIALS; ++i) {
        arm_trial();
        uint64_t onset = present();
        respond(250000);
        const rti_record_t *r = &rti_result()->records[i];
        assert(r->onset_us == onset && r->release_us - onset == 250000);
        assert(r->planned_wait_us >= RTI_MIN_WAIT_US && r->planned_wait_us <= RTI_MAX_WAIT_US);
        step(CONTROL_NONE, RTI_INTERVAL_US);
    }
    assert(rti_get_state() == RTI_READY);
    assert(rti_result()->responses == 0 && rti_result()->response_time_us == 0);
}
int main(void) {
    rti_begin();
    step(CONTROL_NONE, 9000000); assert(rti_get_state() == RTI_INSTRUCTIONS);
    press(); pass_training();
    step(CONTROL_NONE, 9000000); assert(rti_get_state() == RTI_READY);
    press();
    for (int i = 0; i < RTI_TEST_TRIALS; ++i) {
        arm_trial();
        if (i == 1) {
            // Soltura pouco antes do prazo, ainda pendente ao vencer a espera.
            clock_us += planned_wait - 10000;
            button.settled = false;
            uint64_t edge = clock_us;
            step(CONTROL_NONE, 10000);
            assert(rti_get_state() == RTI_FOREPERIOD && !lit);
            button.pressed = false; button.settled = true; button.edge_us = edge;
            step(CONTROL_CONFIRM_RELEASE, 20000);
            assert(rti_result()->records[4].outcome == RTI_ANTICIPATION);
            assert(!rti_result()->records[4].stimulus_shown);
        } else {
            present();
            if (i == 2) {
                step(CONTROL_NONE, RTI_RESPONSE_LIMIT_US);
                assert(rti_result()->records[5].outcome == RTI_OMISSION);
                assert(!rti_result()->records[5].released);
            } else if (i == 3) {
                // Borda antes do limite, aceita depois: ainda é resposta válida.
                clock_us += RTI_RESPONSE_LIMIT_US - 10000;
                button.settled = false;
                uint64_t edge = clock_us;
                step(CONTROL_NONE, 10000); assert(rti_get_state() == RTI_STIMULUS);
                button.pressed = false; button.settled = true; button.edge_us = edge;
                step(CONTROL_CONFIRM_RELEASE, 20000);
                assert(rti_result()->records[6].outcome == RTI_RESPONSE);
            } else respond(250000);
        }
        assert(rti_get_state() == RTI_INTERVAL && !lit);
        step(CONTROL_CONFIRM, RTI_INTERVAL_US - 1);
        assert(rti_get_state() == RTI_INTERVAL);
        step(CONTROL_NONE, 1);
    }
    assert(rti_get_state() == RTI_RESULTS && !lit);
    assert(rti_result()->record_count == RTI_MAX_RECORDS);
    assert(rti_result()->responses == 8 && rti_result()->anticipations == 1 && rti_result()->omissions == 1);
    assert(rti_result()->response_time_us == 7 * 250000ull + 1990000ull);
    assert(strcmp(screen[5], "MEDIA 467 MS") == 0);
    usb = true; rti_serial_update(); rti_serial_update();
    assert(rti_update(CONTROL_BACK, button, clock_us));
    usb = false; rti_serial_update();
    // Repetição do treino e fronteira exata do limite de resposta.
    rti_begin(); press();
    for (int i = 0; i < 3; ++i) {
        arm_trial(); present(); respond(RTI_RESPONSE_LIMIT_US);
        assert(rti_result()->records[i].outcome == RTI_OMISSION);
        step(CONTROL_NONE, RTI_INTERVAL_US);
    }
    assert(rti_get_state() == RTI_TRAINING_FAILED);
    press(); assert(rti_result()->record_count == 0);
    pass_training(); press(); arm_trial(); present();
    // Ruído persistente não pode bloquear o teste indefinidamente.
    button.settled = false;
    step(CONTROL_NONE, RTI_RESPONSE_LIMIT_US + RTI_DEBOUNCE_GRACE_US);
    assert(rti_get_state() == RTI_INTERVAL && rti_result()->omissions == 1);
    step(CONTROL_BACK, 0);
    assert(rti_result()->interrupted && !lit);
    rti_begin(); press(); arm_trial();
    step(CONTROL_BACK, 0);
    assert(rti_result()->records[0].outcome == RTI_ABORTED);
    assert(!rti_result()->records[0].stimulus_shown);
    assert(strcmp(screen[5], "SEM RESPOSTAS") == 0);
    rti_begin(); press(); arm_trial(); present(); step(CONTROL_BACK, 0);
    assert(rti_result()->records[0].outcome == RTI_ABORTED && rti_result()->records[0].stimulus_shown);
    rti_begin(); step(CONTROL_BACK, 0);
    assert(rti_result()->record_count == 0 && rti_result()->interrupted);
    puts("PASS RTI: treino, espera variavel, bordas temporais, respostas, antecipacoes, omissoes e aborto");
}
