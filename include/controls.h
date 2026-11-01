#ifndef CONTROLS_H
#define CONTROLS_H

#include <stdint.h>

typedef enum {
    CONTROL_NONE, CONTROL_UP, CONTROL_DOWN, CONTROL_LEFT, CONTROL_RIGHT, CONTROL_CONFIRM, CONTROL_BACK
} control_event_t;

void controls_init(void);
// Um evento por acionamento. B tem prioridade sobre A e o joystick.
control_event_t controls_poll(uint32_t now_ms);

#endif
