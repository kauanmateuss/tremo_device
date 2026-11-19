#include "patient_assoc.h"
#include <string.h>

static patient_assoc_t assoc;

void patient_assoc_init(void) {
    assoc = (patient_assoc_t){.state = ASSOC_NONE};
    assoc.patient_id[0] = '\0';
    assoc.device_id[0] = '\0';
}

patient_assoc_state_t patient_assoc_state(void) {
    return assoc.state;
}

const char *patient_assoc_patient_id(void) {
    return assoc.patient_id;
}

const char *patient_assoc_device_id(void) {
    return assoc.device_id;
}

bool patient_assoc_request_associate(const char *patient_id) {
    if (!patient_id || strlen(patient_id) == 0 || strlen(patient_id) > PATIENT_ID_MAX)
        return false;
    if (assoc.state == ASSOCIATED && strcmp(assoc.patient_id, patient_id) == 0)
        return false; // Ja associado a este paciente
    strncpy(assoc.patient_id, patient_id, PATIENT_ID_MAX);
    assoc.patient_id[PATIENT_ID_MAX] = '\0';
    assoc.state = ASSOC_PENDING;
    return true;
}

bool patient_assoc_request_disassociate(void) {
    if (assoc.state != ASSOCIATED)
        return false; // Nenhum paciente para desassociar
    assoc.state = DISASSOC_PENDING;
    return true;
}

void patient_assoc_on_confirm_associate(const char *patient_id, bool success) {
    if (assoc.state != ASSOC_PENDING)
        return;
    if (success && patient_id && strcmp(assoc.patient_id, patient_id) == 0) {
        assoc.state = ASSOCIATED;
    } else {
        assoc.state = ASSOC_NONE;
        assoc.patient_id[0] = '\0';
    }
}

void patient_assoc_on_confirm_disassociate(bool success) {
    if (assoc.state != DISASSOC_PENDING)
        return;
    if (success) {
        assoc.state = ASSOC_NONE;
        assoc.patient_id[0] = '\0';
    } else {
        assoc.state = ASSOCIATED;
    }
}

bool patient_assoc_has_patient(void) {
    return assoc.state == ASSOCIATED;
}
