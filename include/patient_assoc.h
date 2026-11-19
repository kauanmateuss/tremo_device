#ifndef PATIENT_ASSOC_H
#define PATIENT_ASSOC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PATIENT_ID_MAX 16
#define DEVICE_ID_MAX 17

// Estado da associacao paciente-dispositivo
typedef enum {
    ASSOC_NONE,           // Nenhum paciente associado
    ASSOCIATED,           // Paciente associado com sucesso
    ASSOC_PENDING,        // Associacao solicitada, aguardando confirmacao
    DISASSOC_PENDING,     // Desassociacao solicitada, aguardando confirmacao
} patient_assoc_state_t;

// Estrutura de associacao atual
typedef struct {
    patient_assoc_state_t state;
    char patient_id[PATIENT_ID_MAX + 1];
    char device_id[DEVICE_ID_MAX + 1];
} patient_assoc_t;

// Inicializa o modulo de associacao
void patient_assoc_init(void);

// Define o ID do dispositivo (chamado pelo communication)
void patient_assoc_set_device_id(const char *device_id);

// Retorna o estado atual da associacao
patient_assoc_state_t patient_assoc_state(void);

// Retorna o ID do paciente associado (ou string vazia se nenhum)
const char *patient_assoc_patient_id(void);

// Retorna o ID do dispositivo
const char *patient_assoc_device_id(void);

// Solicita associacao de um paciente ao dispositivo
bool patient_assoc_request_associate(const char *patient_id);

// Solicita desassociacao do paciente atual
bool patient_assoc_request_disassociate(void);

// Processa confirmacao recebida da placa (chamado pelo MQTT)
void patient_assoc_on_confirm_associate(const char *patient_id, bool success);
void patient_assoc_on_confirm_disassociate(bool success);

// Verifica se ha um paciente associado
bool patient_assoc_has_patient(void);

#endif
