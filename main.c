#include "app.h"
#include "pico/stdlib.h"

int main(void) {
    app_init();
    while (true) {
        app_update();
        sleep_ms(5); // Pequeno intervalo de polling dos controles.
    }
}
