#ifndef APP_H
#define APP_H

typedef enum {
    APP_WELCOME, APP_MENU, APP_INSTRUCTIONS, APP_PREPARING,
    APP_READY, APP_SENSOR_ERROR, APP_ACQUIRING, APP_STOP_CONFIRM,
    APP_RESULTS, APP_UNAVAILABLE, APP_INFO, APP_COG_MENU, APP_MOT,
    APP_COG_UNAVAILABLE, APP_MOT_ERROR, APP_RTI, APP_RTI_ERROR
} app_state_t;

void app_init(void);
void app_update(void);
// Útil para depuração e testes das transições.
app_state_t app_get_state(void);

#endif
