#ifndef MPU_TASK_H
#define MPU_TASK_H
#include <stdbool.h>
bool mpu_task_init(void);
// Libera a tarefa após monitoring_init e o preparo inicial.
void mpu_task_start(void);
#endif
