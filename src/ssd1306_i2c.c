#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"

#include "hardware/i2c.h"
#include "ssd1306_font.h"
#include "ssd1306.h"

// A 400 kHz, 1025 bytes levam cerca de 23 ms. O prazo inclui margem para IRQs.
#define OLED_FRAME_TIMEOUT_US 40000
#define OLED_COMMAND_TIMEOUT_US 2000

// Calcular quanto do buffer será destinado à área de renderização
void calculate_render_area_buffer_length(struct render_area *area) {
    area->buffer_length = (area->end_column - area->start_column + 1) * (area->end_page - area->start_page + 1);
}

// Processo de escrita do i2c espera um byte de controle, seguido por dados
void ssd1306_send_command(uint8_t command) {
    uint8_t buffer[2] = {0x80, command};
    i2c_write_timeout_us(i2c1, ssd1306_i2c_address, buffer, 2, false, OLED_COMMAND_TIMEOUT_US);
}

// Envia uma lista de comandos ao hardware
void ssd1306_send_command_list(uint8_t *ssd, int number) {
    for (int i = 0; i < number; i++) {
        ssd1306_send_command(ssd[i]);
    }
}

// Copia buffer de referência num novo buffer, a fim de adicionar o byte de controle desde o início
static bool send_buffer_until(const uint8_t ssd[], int buffer_length, absolute_time_t until) {
    if (!ssd || buffer_length <= 0 || buffer_length > (int)ssd1306_buffer_length) return false;
    // Buffer fixo: evita malloc e não ocupa a pilha a cada atualização.
    static uint8_t temp_buffer[ssd1306_buffer_length + 1];

    temp_buffer[0] = 0x40;
    memcpy(temp_buffer + 1, ssd, buffer_length);

    return i2c_write_blocking_until(i2c1, ssd1306_i2c_address, temp_buffer,
                                    buffer_length + 1, false, until) == buffer_length + 1;
}

void ssd1306_send_buffer(uint8_t ssd[], int buffer_length) {
    send_buffer_until(ssd, buffer_length, make_timeout_time_us(OLED_FRAME_TIMEOUT_US));
}

// Cria a lista de comandos (com base nos endereços definidos em ssd1306_i2c.h) para a inicialização do display
void ssd1306_init(void) {
    uint8_t commands[] = {
        ssd1306_set_display, ssd1306_set_memory_mode, 0x00,
        ssd1306_set_display_start_line, ssd1306_set_segment_remap | 0x01, 
        ssd1306_set_mux_ratio, ssd1306_height - 1,
        ssd1306_set_common_output_direction | 0x08, ssd1306_set_display_offset,
        0x00, ssd1306_set_common_pin_configuration,
    
#if ((ssd1306_width == 128) && (ssd1306_height == 32))
    0x02,
#elif ((ssd1306_width == 128) && (ssd1306_height == 64))
    0x12,
#else
    0x02,
#endif
        ssd1306_set_display_clock_divide_ratio, 0x80, ssd1306_set_precharge,
        0xF1, ssd1306_set_vcomh_deselect_level, 0x30, ssd1306_set_contrast,
        0xFF, ssd1306_set_entire_on, ssd1306_set_normal_display,
        ssd1306_set_charge_pump, 0x14, ssd1306_set_scroll | 0x00,
        ssd1306_set_display | 0x01,
    };

    ssd1306_send_command_list(commands, count_of(commands));
}

// Cria a lista de comandos para configurar o scrolling
void ssd1306_scroll(bool set) {
    uint8_t commands[] = {
        ssd1306_set_horizontal_scroll | 0x00, 0x00, 0x00, 0x00, 0x03,
        0x00, 0xFF, ssd1306_set_scroll | (set ? 0x01 : 0)
    };

    ssd1306_send_command_list(commands, count_of(commands));
}

// Atualiza uma parte do display com uma área de renderização
bool render_on_display(uint8_t *ssd, struct render_area *area) {
    // Um único prazo para o endereçamento e os pixels. 0x00 inicia uma lista de comandos.
    absolute_time_t until = make_timeout_time_us(OLED_FRAME_TIMEOUT_US);
    uint8_t commands[] = {0x00,
        ssd1306_set_column_address, area->start_column, area->end_column,
        ssd1306_set_page_address, area->start_page, area->end_page
    };

    if (i2c_write_blocking_until(i2c1, ssd1306_i2c_address, commands, sizeof(commands),
                                 false, until) != (int)sizeof(commands)) return false;
    return send_buffer_until(ssd, area->buffer_length, until);
}

// Determina o pixel a ser aceso (no display) de acordo com a coordenada fornecida
void ssd1306_set_pixel(uint8_t *ssd, int x, int y, bool set) {
    if (!ssd || x < 0 || x >= ssd1306_width || y < 0 || y >= ssd1306_height) return;

    const int bytes_per_row = ssd1306_width;

    int byte_idx = (y / 8) * bytes_per_row + x;
    uint8_t byte = ssd[byte_idx];

    if (set) {
        byte |= 1 << (y % 8);
    }
    else {
        byte &= ~(1 << (y % 8));
    }

    ssd[byte_idx] = byte;
}

// Algoritmo de Bresenham básico
void ssd1306_draw_line(uint8_t *ssd, int x_0, int y_0, int x_1, int y_1, bool set) {
    int dx = abs(x_1 - x_0); // Deslocamentos
    int dy = -abs(y_1 - y_0);
    int sx = x_0 < x_1 ? 1 : -1; // Direção de avanço
    int sy = y_0 < y_1 ? 1 : -1;
    int error = dx + dy; // Erro acumulado
    int error_2;

    while (true) {
        ssd1306_set_pixel(ssd, x_0, y_0, set); // Acende pixel no ponto atual
        if (x_0 == x_1 && y_0 == y_1) {
            break; // Verifica se o ponto final foi alcançado
        }

        error_2 = 2 * error; // Ajusta o erro acumulado

        if (error_2 >= dy) {
            error += dy;
            x_0 += sx; // Avança na direção x
        }
        if (error_2 <= dx) {
            error += dx;
            y_0 += sy; // Avança na direção y
        }
    }
}

// Adquire os pixels para um caractere (de acordo com ssd1306_font.h)
static int ssd1306_get_font(uint8_t character)
{
  if (character >= 'A' && character <= 'Z') {
    return character - 'A' + 1;
  }
  else if (character >= '0' && character <= '9') {
    return character - '0' + 27;
  }
  else if (character == '-') return 37;
  else if (character == '.') return 38;
  else
    return 0;
}

// Desenha um único caractere no display
void ssd1306_draw_char(uint8_t *ssd, int16_t x, int16_t y, uint8_t character) {
    if (!ssd || x < 0 || y < 0 || x > ssd1306_width - 8 || y > ssd1306_height - 8) {
        return;
    }

    y = y / 8;

    character = toupper(character);
    int idx = ssd1306_get_font(character);
    int fb_idx = y * ssd1306_width + x;

    for (int i = 0; i < 8; i++) {
        ssd[fb_idx++] = font[idx * 8 + i];
    }
}

// Desenha uma string, chamando a função de desenhar caractere várias vezes
void ssd1306_draw_string(uint8_t *ssd, int16_t x, int16_t y, const char *string) {
    if (!ssd || x < 0 || y < 0 || x > ssd1306_width - 8 || y > ssd1306_height - 8) {
        return;
    }

    if (!string) return;
    while (*string && x <= ssd1306_width - 8) {
        ssd1306_draw_char(ssd, x, y, *string++);
        x += 8;
    }
}

// Comando de configuração com base na estrutura ssd1306_t
void ssd1306_command(ssd1306_t *ssd, uint8_t command) {
  ssd->port_buffer[1] = command;
  i2c_write_timeout_us(ssd->i2c_port, ssd->address, ssd->port_buffer, 2, false, OLED_COMMAND_TIMEOUT_US);
}

// Função de configuração do display para o caso do bitmap
void ssd1306_config(ssd1306_t *ssd) {
    ssd1306_command(ssd, ssd1306_set_display | 0x00);
    ssd1306_command(ssd, ssd1306_set_memory_mode);
    ssd1306_command(ssd, 0x01);
    ssd1306_command(ssd, ssd1306_set_display_start_line | 0x00);
    ssd1306_command(ssd, ssd1306_set_segment_remap | 0x01);
    ssd1306_command(ssd, ssd1306_set_mux_ratio);
    ssd1306_command(ssd, ssd1306_height - 1);
    ssd1306_command(ssd, ssd1306_set_common_output_direction | 0x08);
    ssd1306_command(ssd, ssd1306_set_display_offset);
    ssd1306_command(ssd, 0x00);
    ssd1306_command(ssd, ssd1306_set_common_pin_configuration);
    ssd1306_command(ssd, 0x12);
    ssd1306_command(ssd, ssd1306_set_display_clock_divide_ratio);
    ssd1306_command(ssd, 0x80);
    ssd1306_command(ssd, ssd1306_set_precharge);
    ssd1306_command(ssd, 0xF1);
    ssd1306_command(ssd, ssd1306_set_vcomh_deselect_level);
    ssd1306_command(ssd, 0x30);
    ssd1306_command(ssd, ssd1306_set_contrast);
    ssd1306_command(ssd, 0xFF);
    ssd1306_command(ssd, ssd1306_set_entire_on);
    ssd1306_command(ssd, ssd1306_set_normal_display);
    ssd1306_command(ssd, ssd1306_set_charge_pump);
    ssd1306_command(ssd, 0x14);
    ssd1306_command(ssd, ssd1306_set_display | 0x01);
}

// Inicializa o display para o caso de exibição de bitmap
bool ssd1306_init_bm(ssd1306_t *ssd, uint8_t width, uint8_t height, bool external_vcc, uint8_t address, i2c_inst_t *i2c) {
    if (!ssd || !i2c || width == 0 || width > ssd1306_width ||
        height == 0 || height > ssd1306_height || height % 8 != 0) return false;
    ssd->external_vcc = external_vcc;
    ssd->width = width;
    ssd->height = height;
    ssd->pages = height / 8U;
    ssd->address = address;
    ssd->i2c_port = i2c;
    ssd->bufsize = ssd->pages * ssd->width + 1;
    ssd->ram_buffer = calloc(ssd->bufsize, sizeof(uint8_t));
    if (!ssd->ram_buffer) return false;
    ssd->ram_buffer[0] = 0x40;
    ssd->port_buffer[0] = 0x80;
    return true;
}

// Envia os dados ao display
void ssd1306_send_data(ssd1306_t *ssd) {
    if (!ssd || !ssd->ram_buffer) return;
    ssd1306_command(ssd, ssd1306_set_column_address);
    ssd1306_command(ssd, 0);
    ssd1306_command(ssd, ssd->width - 1);
    ssd1306_command(ssd, ssd1306_set_page_address);
    ssd1306_command(ssd, 0);
    ssd1306_command(ssd, ssd->pages - 1);
    i2c_write_timeout_us(ssd->i2c_port, ssd->address, ssd->ram_buffer, ssd->bufsize, false, OLED_FRAME_TIMEOUT_US);
}

// Desenha o bitmap (a ser fornecido em display_oled.c) no display
void ssd1306_draw_bitmap(ssd1306_t *ssd, const uint8_t *bitmap) {
    if (!ssd || !ssd->ram_buffer || !bitmap) return;
    memcpy(ssd->ram_buffer + 1, bitmap, ssd->bufsize - 1);
    ssd1306_send_data(ssd);
}
