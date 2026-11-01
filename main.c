#include "pico/stdlib.h"
#include "display.h"
#include "monitoring.h"

#define WELCOME_DURATION_MS 2000

int main(void) {
    stdio_init_all();
    display_init();
    display_show_welcome();
    sleep_ms(WELCOME_DURATION_MS);

    monitoring_init();
    while (true) {
        monitoring_update();
    }
}
