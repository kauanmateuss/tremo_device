#ifndef COMMUNICATION_H
#define COMMUNICATION_H
#include <stdbool.h>
void communication_init(void);
// false: suspende a rede antes de qualquer trecho cronometrado cognitivo.
void communication_update(bool allowed);
#endif
