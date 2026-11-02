#ifndef PATTERNS_H
#define PATTERNS_H

#include <stdint.h>
#define PATTERN_COUNT 16

// Banco local v1: formas 3x3, sem significado clínico, centradas na matriz 5x5.
// Limpa/desenha somente o buffer, para compor padrões e posições no mesmo envio.
void patterns_draw(uint8_t id);
void patterns_show(uint8_t id);
void patterns_clear(void);
// Fisher-Yates, in-place. O chamador fornece IDs distintos e tamanho válido.
void patterns_shuffle(uint8_t *items, uint8_t count);

#endif
