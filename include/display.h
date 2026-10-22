#ifndef DISPLAY_H
#define DISPLAY_H


// Pinos do display
#define I2C_SDA 14
#define I2C_SCL 15

// =============== CABECALHOS DAS FUNCOES ===============

void display_init();

// Funcao para zerar a tela do display
void clear_display();

// Funcao para escrever texto no display
void display_show_lines(const char *text[], size_t count);

// funcao para escrever texto no display
void display_draw_text(const char *text, uint8_t x, uint8_t y);

// funcao para escrever no display
void display_render();

#endif // DISPLAY_OLED_H