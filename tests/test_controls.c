#include <assert.h>
#include <stdio.h>
#include "controls.h"
#include "pico/stdlib.h"

static bool level_a = true, level_b = true;
static uint16_t analog = 2048, analog_x = 2048;
static uint adc_input;
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
    assert(controls_poll(0) == CONTROL_NONE);
    level_a = false;
    assert(controls_poll(10) == CONTROL_NONE);
    level_a = true; // Bounce.
    assert(controls_poll(20) == CONTROL_NONE);
    level_a = false;
    assert(controls_poll(25) == CONTROL_NONE);
    assert(controls_poll(54) == CONTROL_NONE);
    assert(controls_poll(55) == CONTROL_CONFIRM);
    assert(controls_poll(1000) == CONTROL_NONE); // Segurar não repete.
    level_a = true;
    assert(controls_poll(1010) == CONTROL_NONE);
    assert(controls_poll(1040) == CONTROL_NONE);
    level_a = level_b = false;
    assert(controls_poll(1050) == CONTROL_NONE);
    assert(controls_poll(1080) == CONTROL_BACK); // B tem prioridade.
    level_a = level_b = true;
    controls_poll(1100); controls_poll(1130);
    analog = 4095;
    assert(controls_poll(1200) == CONTROL_NONE);
    assert(controls_poll(1230) == CONTROL_UP);
    assert(controls_poll(2000) == CONTROL_NONE);
    analog = 0; // Só deve rearmar depois de voltar ao centro.
    controls_poll(2010);
    assert(controls_poll(2040) == CONTROL_NONE);
    analog = 2048;
    controls_poll(2050); controls_poll(2080);
    analog = 0;
    controls_poll(2100);
    assert(controls_poll(2130) == CONTROL_DOWN);
    analog = 2048;
    controls_poll(2200); controls_poll(2230);
    analog_x = 4095;
    controls_poll(2250);
    assert(controls_poll(2280) == CONTROL_LEFT);
    assert(controls_poll(2400) == CONTROL_NONE);
    analog_x = 2048;
    controls_poll(2420); controls_poll(2450);
    analog_x = 0;
    controls_poll(2460);
    assert(controls_poll(2490) == CONTROL_RIGHT);
    analog_x = 2048;
    controls_poll(2500); controls_poll(2530);
    analog = 4095; analog_x = 0; // Diagonal: vertical ganha.
    controls_poll(2540);
    assert(controls_poll(2570) == CONTROL_UP);
    analog = analog_x = 2048;
    level_a = false;
    controls_init(); // Já segurado ao ligar: não dispara.
    assert(controls_poll(3000) == CONTROL_NONE);
    puts("PASS controles: debounce, prioridade, repeticao e centro do joystick");
}
