#include "patient_assoc.h"
#include <stdio.h>
#include <string.h>
#ifdef TREMO_FREERTOS_ENABLED
#include "FreeRTOS.h"
#include "task.h"
#else
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#endif

static char device_id[17], patient_id[PATIENT_ID_MAX + 1];

void patient_assoc_set_device_id(const char *device) {
    snprintf(device_id, sizeof(device_id), "%s", device);
}
const char *patient_assoc_device_id(void) { return device_id; }

void patient_assoc_set(const char *patient) {
    taskENTER_CRITICAL();
    snprintf(patient_id, sizeof(patient_id), "%s", patient);
    taskEXIT_CRITICAL();
}
void patient_assoc_copy(char patient[PATIENT_ID_MAX + 1]) {
    taskENTER_CRITICAL();
    memcpy(patient, patient_id, sizeof(patient_id));
    taskEXIT_CRITICAL();
}
