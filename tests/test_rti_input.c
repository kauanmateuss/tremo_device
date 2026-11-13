// Integra o debounce real com o RTI; apenas GPIO, ADC, relógio e saídas são substituídos.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rti.h"
#include "matrix.h"

static uint64_t clock_us;
static bool a_high = true, b_high = true, lit;
uint64_t time_us_64(void) { return clock_us; }
bool stdio_usb_connected(void) { return false; }
uint32_t get_rand_32(void) { return 0; }
void gpio_init(uint pin) { (void)pin; }
void gpio_set_dir(uint pin, bool out) { (void)pin; (void)out; }
void gpio_pull_up(uint pin) { (void)pin; }
bool gpio_get(uint pin) { return pin == 5 ? a_high : pin == 6 ? b_high : true; }
void adc_init(void) {}
void adc_gpio_init(uint pin) { (void)pin; }
void adc_select_input(uint input) { (void)input; }
uint16_t adc_read(void) { return 2048; }
void npClear(void) { lit = false; }
void npWrite(void) { clock_us += 1000; }
uint64_t npWriteTimed(void) { npWrite(); return time_us_64(); }
void npSetPixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b) {
    (void)x; (void)y; (void)r; (void)g; (void)b; lit = true;
}
void display_show_lines(const char *lines[], size_t n) {
    for (size_t i = 0; i < n; ++i) assert(strlen(lines[i]) <= 15);
    clock_us += 25000;
}
static void poll_step(uint64_t elapsed) {
    clock_us += elapsed;
    control_event_t event = controls_poll((uint32_t)(clock_us / 1000));
    assert(!rti_update(event, controls_confirm_state(), clock_us));
}
static void transition_a(bool high) {
    a_high = high;
    poll_step(5000);
    poll_step(30000);
}
int main(void) {
    controls_init(); rti_begin();
    transition_a(false); assert(rti_get_state() == RTI_WAIT_RELEASE);
    poll_step(100000); assert(rti_get_state() == RTI_WAIT_RELEASE);
    transition_a(true); assert(rti_get_state() == RTI_WAIT_HOLD);
    transition_a(false); assert(rti_get_state() == RTI_FOREPERIOD);
    a_high = true;
    poll_step(990000); // Soltura antes do verde, debounce ainda não concluiu.
    uint64_t first_edge = clock_us;
    poll_step(10000);
    assert(rti_get_state() == RTI_FOREPERIOD && !lit);
    poll_step(20000);
    assert(rti_get_state() == RTI_INTERVAL);
    assert(rti_result()->records[0].outcome == RTI_ANTICIPATION);
    assert(rti_result()->records[0].release_us == first_edge);
    assert(!rti_result()->records[0].stimulus_shown);
    poll_step(RTI_INTERVAL_US); poll_step(5000);
    assert(rti_get_state() == RTI_WAIT_HOLD);
    transition_a(false);
    clock_us += RTI_MIN_WAIT_US;
    assert(!rti_update(CONTROL_LEFT, controls_confirm_state(), clock_us));
    assert(rti_get_state() == RTI_FOREPERIOD && !lit);
    poll_step(0);
    assert(rti_get_state() == RTI_STIMULUS && lit);
    uint64_t onset = clock_us;
    a_high = true; poll_step(250000); // Primeira soltura, seguida de bounce.
    a_high = false; poll_step(10000);
    a_high = true; poll_step(5000);
    uint64_t stable_edge = clock_us;
    poll_step(30000);
    assert(rti_get_state() == RTI_INTERVAL && !lit);
    const rti_record_t *r = &rti_result()->records[1];
    assert(r->outcome == RTI_RESPONSE && r->onset_us == onset);
    assert(r->release_us == stable_edge && r->release_us - onset == 265000);
    poll_step(RTI_INTERVAL_US); poll_step(5000);
    transition_a(false); poll_step(RTI_MIN_WAIT_US);
    onset = clock_us;
    a_high = true; poll_step(RTI_RESPONSE_LIMIT_US - 10000);
    poll_step(10000); // Prazo vencido, aguarda validação da borda prévia.
    assert(rti_get_state() == RTI_STIMULUS);
    // Um evento antigo da fila não deve encerrar a tentativa antes da soltura.
    control_button_state_t queued = {.pressed = true, .settled = true};
    assert(!rti_update(CONTROL_RIGHT, queued, clock_us + 20000));
    assert(rti_get_state() == RTI_STIMULUS);
    poll_step(20000);
    assert(rti_result()->records[2].outcome == RTI_RESPONSE);
    assert(rti_result()->records[2].release_us - onset == RTI_RESPONSE_LIMIT_US - 10000);
    puts("PASS RTI + controles reais: antecipacao pendente, bounce e borda antes do prazo");
}
