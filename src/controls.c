#include "controls.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"

#define BUTTON_A_PIN 5
#define BUTTON_B_PIN 6
#define JOYSTICK_Y_PIN 26
#define JOYSTICK_Y_ADC 0
#define DEBOUNCE_MS 30
#define JOYSTICK_LOW 1000
#define JOYSTICK_HIGH 3000
#define JOYSTICK_CENTER_LOW 1500
#define JOYSTICK_CENTER_HIGH 2500

typedef struct {
    bool raw;
    bool stable;
    uint32_t changed_ms;
} button_t;

static button_t button_a, button_b;
static int joystick_raw, joystick_stable;
static uint32_t joystick_changed_ms;
static bool joystick_armed;

static bool pressed(button_t *button, uint pin, uint32_t now) {
    bool raw = !gpio_get(pin); // Pull-up: pressionado = nível baixo.
    if (raw != button->raw) {
        button->raw = raw;
        button->changed_ms = now;
    }
    if (raw != button->stable && now - button->changed_ms >= DEBOUNCE_MS) {
        button->stable = raw;
        return raw; // Só gera evento na borda de pressionamento.
    }
    return false;
}

void controls_init(void) {
    gpio_init(BUTTON_A_PIN);
    gpio_set_dir(BUTTON_A_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_A_PIN);
    gpio_init(BUTTON_B_PIN);
    gpio_set_dir(BUTTON_B_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_B_PIN);
    // Botão já segurado ao ligar não deve confirmar uma opção.
    button_a = (button_t){.raw = !gpio_get(BUTTON_A_PIN), .stable = !gpio_get(BUTTON_A_PIN)};
    button_b = (button_t){.raw = !gpio_get(BUTTON_B_PIN), .stable = !gpio_get(BUTTON_B_PIN)};
    adc_init();
    adc_gpio_init(JOYSTICK_Y_PIN);
    joystick_raw = joystick_stable = 0;
    joystick_changed_ms = 0;
    joystick_armed = true;
}

control_event_t controls_poll(uint32_t now_ms) {
    bool a = pressed(&button_a, BUTTON_A_PIN, now_ms);
    bool b = pressed(&button_b, BUTTON_B_PIN, now_ms);
    adc_select_input(JOYSTICK_Y_ADC);
    uint16_t value = adc_read();
    // Histerese: uma faixa central mais estreita evita oscilações no limiar.
    int direction = joystick_raw;
    if (value < JOYSTICK_LOW) direction = 1; // Para baixo.
    else if (value > JOYSTICK_HIGH) direction = -1; // Para cima.
    else if (value >= JOYSTICK_CENTER_LOW && value <= JOYSTICK_CENTER_HIGH) direction = 0;
    if (direction != joystick_raw) {
        joystick_raw = direction;
        joystick_changed_ms = now_ms;
    }
    control_event_t event = CONTROL_NONE;
    if (now_ms - joystick_changed_ms >= DEBOUNCE_MS && direction != joystick_stable) {
        joystick_stable = direction;
        if (direction == 0) joystick_armed = true;
        else if (joystick_armed) {
            event = direction < 0 ? CONTROL_UP : CONTROL_DOWN;
            joystick_armed = false;
        }
    }
    if (b) return CONTROL_BACK;
    if (a) return CONTROL_CONFIRM;
    return event;
}
