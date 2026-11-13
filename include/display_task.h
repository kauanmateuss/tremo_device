#ifndef DISPLAY_TASK_H
#define DISPLAY_TASK_H

#include "ssd1306.h"
#include "matrix.h"

// Cria a tarefa e seu canal antes de iniciar o scheduler.
bool display_task_init(void);
// Uso em contexto de tarefa. Retorna após concluir a transferência.
// O buffer permanece válido e estável durante toda a chamada.
bool display_task_render(uint8_t *buffer, struct render_area *area);

uint64_t display_task_matrix(const npLED_t *pixels);

#endif
