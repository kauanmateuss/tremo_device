#include "ssd1306.h"
#include "hardware/i2c.h"
#include "display.h"
#include <string.h>

static uint8_t ssd[ssd1306_buffer_length];
static struct render_area frame_area;

// Funcao para inicializar o display
void display_init() {
    // inicialização do i2c
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // processo de inicialização completo
    ssd1306_init();

    // preparação da area de renderização para o display
    frame_area.start_column = 0;
    frame_area.end_column = ssd1306_width - 1;
    frame_area.start_page = 0;
    frame_area.end_page = ssd1306_n_pages - 1;

    calculate_render_area_buffer_length(&frame_area);

    // limpa o buffer
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);
}


// Funcao para escrever texto no display
void display_show_lines(const char *text[], size_t count) {
    uint8_t y = 0;

    memset(ssd, 0, sizeof(ssd));

    for (size_t i = 0; i < count; i++) {
        ssd1306_draw_string(ssd, 5, y, (char *)text[i]);
        y += 8;
    }

    render_on_display(ssd, &frame_area);
}


// funcao para escrever texto no display
void display_draw_text(const char *text, uint8_t x, uint8_t y) {
    ssd1306_draw_string(ssd, x, y, text);
}


// Funcao para zerar a tela do display
void clear_display() {
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);
}


// funcao para escrever no display
void display_render(){
    render_on_display(ssd, &frame_area);
}