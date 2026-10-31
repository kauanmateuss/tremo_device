#ifndef ASSESSMENT_H
#define ASSESSMENT_H
#include "controls.h"
// Índices do menu: 0 MOT, 1 RTI, 2 PRM, 3 PAL.
void assessment_begin(unsigned test);
bool assessment_update(control_event_t event, uint64_t now_us);
bool assessment_finished(void);
bool assessment_interrupted(void);
#endif
