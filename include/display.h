#ifndef DISPLAY_H
#define DISPLAY_H

#include <stddef.h>
#include <stdint.h>


// Pinos do display
#define I2C_SDA 14
#define I2C_SCL 15

// =============== CABECALHOS DAS FUNCOES ===============

void display_init(void);

// Mostra a tela de boas-vindas; o tempo de exibição fica a cargo da aplicação.
void display_show_welcome(void);

// Funcao para zerar a tela do display
void clear_display(void);

// Funcao para escrever texto no display
void display_show_lines(const char *text[], size_t count);

// Altera somente o buffer. y é alinhado para baixo em múltiplos de 8.
void display_draw_text(const char *text, uint8_t x, uint8_t y);

// funcao para escrever no display
void display_render(void);

#endif // DISPLAY_H
