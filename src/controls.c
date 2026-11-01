#include "controls.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"

#define BUTTON_A_PIN 5
#define BUTTON_B_PIN 6
#define JOYSTICK_Y_PIN 26
#define JOYSTICK_Y_ADC 0
#define JOYSTICK_X_PIN 27
#define JOYSTICK_X_ADC 1
#define DEBOUNCE_MS 30
#define JOYSTICK_LOW 1000
#define JOYSTICK_HIGH 3000
#define JOYSTICK_CENTER_LOW 1500
#define JOYSTICK_CENTER_HIGH 2500

typedef struct {
    bool raw;
    bool stable;
    uint32_t changed_ms;
    uint64_t changed_us;
    uint64_t edge_us;
} button_t;

static button_t button_a, button_b;
static int joystick_raw, joystick_stable;
static uint32_t joystick_changed_ms;
static bool joystick_armed;
static bool confirm_settled;

static bool update_button(button_t *button, uint pin, uint32_t now) {
    bool raw = !gpio_get(pin); // Pull-up: pressionado = nível baixo.
    if (raw != button->raw) {
        button->raw = raw;
        button->changed_ms = now;
        button->changed_us = time_us_64();
    }
    if (raw != button->stable && now - button->changed_ms >= DEBOUNCE_MS) {
        button->stable = raw;
        button->edge_us = button->changed_us;
        return true; // Informa tanto pressionamento quanto soltura.
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
    confirm_settled = false;
    adc_init();
    adc_gpio_init(JOYSTICK_Y_PIN);
    adc_gpio_init(JOYSTICK_X_PIN);
    joystick_raw = joystick_stable = 0;
    joystick_changed_ms = 0;
    joystick_armed = true;
}

control_event_t controls_poll(uint32_t now_ms) {
    bool a_changed = update_button(&button_a, BUTTON_A_PIN, now_ms);
    bool b_changed = update_button(&button_b, BUTTON_B_PIN, now_ms);
    confirm_settled = button_a.raw == button_a.stable &&
                      now_ms - button_a.changed_ms >= DEBOUNCE_MS;
    adc_select_input(JOYSTICK_Y_ADC);
    uint16_t y = adc_read();
    adc_select_input(JOYSTICK_X_ADC);
    uint16_t x = adc_read();
    // Histerese e rearmamento somente com ambos os eixos no centro.
    // Em diagonal, o eixo vertical tem prioridade. VERT/HORZ altos = cima/esquerda.
    int direction = joystick_raw;
    if (y < JOYSTICK_LOW) direction = 1;
    else if (y > JOYSTICK_HIGH) direction = -1;
    else if (x < JOYSTICK_LOW) direction = 2;
    else if (x > JOYSTICK_HIGH) direction = -2;
    else if (y >= JOYSTICK_CENTER_LOW && y <= JOYSTICK_CENTER_HIGH &&
             x >= JOYSTICK_CENTER_LOW && x <= JOYSTICK_CENTER_HIGH) direction = 0;
    if (direction != joystick_raw) {
        joystick_raw = direction;
        joystick_changed_ms = now_ms;
    }
    control_event_t event = CONTROL_NONE;
    if (now_ms - joystick_changed_ms >= DEBOUNCE_MS && direction != joystick_stable) {
        joystick_stable = direction;
        if (direction == 0) joystick_armed = true;
        else if (joystick_armed) {
            if (direction == -1) event = CONTROL_UP;
            else if (direction == 1) event = CONTROL_DOWN;
            else if (direction == -2) event = CONTROL_LEFT;
            else event = CONTROL_RIGHT;
            joystick_armed = false;
        }
    }
    if (b_changed && button_b.stable) return CONTROL_BACK;
    if (a_changed) return button_a.stable ? CONTROL_CONFIRM : CONTROL_CONFIRM_RELEASE;
    return event;
}

control_button_state_t controls_confirm_state(void) {
    return (control_button_state_t){.pressed = button_a.stable,
        .settled = confirm_settled, .edge_us = button_a.edge_us};
}
