#ifndef MATRIX_H
#define MATRIX_H

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


// Retorna false se não houver recursos PIO ou se o pino for inválido.
bool npInit(uint pin);
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b);
// Altera somente o buffer; use npWrite para atualizar os LEDs.
void npClear(void);
void npWrite(void);

#endif // MATRIX_H
