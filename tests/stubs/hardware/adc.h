#ifndef TEST_ADC_H
#define TEST_ADC_H
#include <stdint.h>
void adc_init(void);
void adc_gpio_init(unsigned int pin);
void adc_select_input(unsigned int input);
uint16_t adc_read(void);
#endif
