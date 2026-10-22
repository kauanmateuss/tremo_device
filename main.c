#include <stdio.h>
#include "pico/stdlib.h"
#include "matrix.h"
#include "display.h"
#include "hardware/i2c.h"
#include "mpu6050_i2c.h"



int main()
{
    stdio_init_all();

    while (true) {
        printf("Hello, world!\n");
        sleep_ms(1000);
    }
}
