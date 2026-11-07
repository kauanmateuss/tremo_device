#ifndef APP_H
#define APP_H

#include "controls.h"

typedef enum {
    APP_WELCOME, APP_MENU, APP_INSTRUCTIONS, APP_PREPARING,
    APP_READY, APP_SENSOR_ERROR, APP_ACQUIRING, APP_STOP_CONFIRM,
    APP_RESULTS, APP_UNAVAILABLE, APP_INFO, APP_COG_MENU, APP_MOT,
    APP_COG_UNAVAILABLE, APP_MOT_ERROR, APP_RTI, APP_RTI_ERROR, APP_PRM, APP_PRM_ERROR, APP_PAL, APP_PAL_ERROR, APP_PAUSED, APP_CALIBRATING, APP_CAL_ERROR, APP_EVAL_SETUP, APP_EVAL_RESULTS
} app_state_t;

void app_init(void);
void app_update(void);
// Entrada capturada pela TaskInput. event_us é o instante de processamento.
void app_update_input(control_event_t event, control_button_state_t button, uint64_t event_us);
// Útil para depuração e testes das transições.
app_state_t app_get_state(void);

#endif
