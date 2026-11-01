#ifndef SSD1306_H
#define SSD1306_H

#include "ssd1306_i2c.h"
void calculate_render_area_buffer_length(struct render_area *area);
void ssd1306_send_command(uint8_t cmd);
void ssd1306_send_command_list(uint8_t *ssd, int number);
void ssd1306_send_buffer(uint8_t ssd[], int buffer_length);
void ssd1306_init(void);
void ssd1306_scroll(bool set);
bool render_on_display(uint8_t *ssd, struct render_area *area);
void ssd1306_set_pixel(uint8_t *ssd, int x, int y, bool set);
void ssd1306_draw_line(uint8_t *ssd, int x_0, int y_0, int x_1, int y_1, bool set);
void ssd1306_draw_char(uint8_t *ssd, int16_t x, int16_t y, uint8_t character);
void ssd1306_draw_string(uint8_t *ssd, int16_t x, int16_t y, const char *string);
void ssd1306_command(ssd1306_t *ssd, uint8_t command);
void ssd1306_config(ssd1306_t *ssd);
bool ssd1306_init_bm(ssd1306_t *ssd, uint8_t width, uint8_t height, bool external_vcc, uint8_t address, i2c_inst_t *i2c);
void ssd1306_send_data(ssd1306_t *ssd);
void ssd1306_draw_bitmap(ssd1306_t *ssd, const uint8_t *bitmap);

#endif
