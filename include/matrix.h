#ifndef MATRIX_H
#define MATRIX_H

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"

// Definição do numero de LEDs e pino.
#define LED_COUNT 25
#define LED_PIN 7

// Definicao de pixel GRB
typedef struct {

  uint8_t G; 
  uint8_t R; 
  uint8_t B;
  
} npLED_t;


void npInit(uint pin);
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b);
void npClear();
void npWrite();

#endif // MATRIX_H