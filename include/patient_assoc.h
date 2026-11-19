#ifndef PATIENT_ASSOC_H
#define PATIENT_ASSOC_H
#define PATIENT_ID_MAX 16
void patient_assoc_set_device_id(const char *device);
const char *patient_assoc_device_id(void);
void patient_assoc_set(const char *patient);
void patient_assoc_copy(char patient[PATIENT_ID_MAX + 1]);
#endif
