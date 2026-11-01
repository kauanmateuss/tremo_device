#include <assert.h>
#include <stdio.h>
#include "controls.h"
#include "pico/stdlib.h"

static bool level_a = true, level_b = true;
static uint16_t analog = 2048, analog_x = 2048;
static uint adc_input;
static uint64_t clock_us;
uint64_t time_us_64(void) { return clock_us; }
static control_event_t poll(uint32_t now) {
    clock_us = (uint64_t)now * 1000;
    return controls_poll(now);
}
void gpio_init(uint pin) { assert(pin == 5 || pin == 6); }
void gpio_set_dir(uint pin, bool out) { (void)pin; assert(!out); }
void gpio_pull_up(uint pin) { (void)pin; }
bool gpio_get(uint pin) { return pin == 5 ? level_a : level_b; }
void adc_init(void) {}
void adc_gpio_init(uint pin) { assert(pin == 26 || pin == 27); }
void adc_select_input(uint input) { assert(input < 2); adc_input = input; }
uint16_t adc_read(void) { return adc_input == 0 ? analog : analog_x; }

int main(void) {
    controls_init();
    assert(poll(0) == CONTROL_NONE);
    level_a = false;
    assert(poll(10) == CONTROL_NONE);
    level_a = true; // Bounce.
    assert(poll(20) == CONTROL_NONE);
    level_a = false;
    assert(poll(25) == CONTROL_NONE);
    assert(poll(54) == CONTROL_NONE);
    assert(poll(55) == CONTROL_CONFIRM);
    assert(controls_confirm_state().pressed && controls_confirm_state().settled);
    assert(controls_confirm_state().edge_us == 25000);
    assert(poll(1000) == CONTROL_NONE); // Segurar não repete.
    level_a = true;
    assert(poll(1010) == CONTROL_NONE);
    assert(!controls_confirm_state().settled);
    assert(poll(1040) == CONTROL_CONFIRM_RELEASE);
    assert(!controls_confirm_state().pressed && controls_confirm_state().settled);
    assert(controls_confirm_state().edge_us == 1010000);
    level_a = level_b = false;
    assert(poll(1050) == CONTROL_NONE);
    assert(poll(1080) == CONTROL_BACK); // B tem prioridade.
    level_a = level_b = true;
    poll(1100); poll(1130);
    analog = 4095;
    assert(poll(1200) == CONTROL_NONE);
    assert(poll(1230) == CONTROL_UP);
    assert(poll(2000) == CONTROL_NONE);
    analog = 0; // Só deve rearmar depois de voltar ao centro.
    poll(2010);
    assert(poll(2040) == CONTROL_NONE);
    analog = 2048;
    poll(2050); poll(2080);
    analog = 0;
    poll(2100);
    assert(poll(2130) == CONTROL_DOWN);
    analog = 2048;
    poll(2200); poll(2230);
    analog_x = 4095;
    poll(2250);
    assert(poll(2280) == CONTROL_LEFT);
    assert(poll(2400) == CONTROL_NONE);
    analog_x = 2048;
    poll(2420); poll(2450);
    analog_x = 0;
    poll(2460);
    assert(poll(2490) == CONTROL_RIGHT);
    analog_x = 2048;
    poll(2500); poll(2530);
    analog = 4095; analog_x = 0; // Diagonal: vertical ganha.
    poll(2540);
    assert(poll(2570) == CONTROL_UP);
    analog = analog_x = 2048;
    level_a = false;
    controls_init(); // Já segurado ao ligar: não dispara.
    assert(poll(3000) == CONTROL_NONE);
    puts("PASS controles: debounce, prioridade, repeticao e centro do joystick");
}
