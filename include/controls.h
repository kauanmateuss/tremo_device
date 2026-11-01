#ifndef CONTROLS_H
#define CONTROLS_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CONTROL_NONE, CONTROL_UP, CONTROL_DOWN, CONTROL_LEFT, CONTROL_RIGHT, CONTROL_CONFIRM, CONTROL_BACK, CONTROL_CONFIRM_RELEASE, CONTROL_PAUSE
} control_event_t;

typedef struct {
    bool pressed; // Estado de A validado pelo debounce.
    bool settled; // Nível estável há pelo menos 30 ms, sem transição pendente.
    uint64_t edge_us; // Primeira observação da última borda validada de A.
} control_button_state_t;

void controls_init(void);
control_button_state_t controls_confirm_state(void);
// Um evento por acionamento. B tem prioridade sobre A e o joystick.
control_event_t controls_poll(uint32_t now_ms);

#endif
