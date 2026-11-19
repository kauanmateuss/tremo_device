#ifndef COMMUNICATION_H
#define COMMUNICATION_H
#include <stdbool.h>
#include <stddef.h>
typedef enum {
    COMM_SUSPENDED,     // Teste cognitivo: não processar a rede.
    COMM_NORMAL,        // Menu/resultados: conectar, reconectar e transmitir.
    COMM_STREAM_ONLY    // Aquisição: usar apenas uma conexão já estabelecida.
} communication_mode_t;

void communication_init(void);
typedef enum {
    COMM_NO_RADIO, COMM_WIFI_WAIT, COMM_WIFI_NOT_FOUND, COMM_WIFI_BAD_AUTH,
    COMM_WIFI_NO_IP, COMM_BROKER_WAIT, COMM_ONLINE
} communication_status_t;
communication_status_t communication_status(void);
// Com RTOS, o serviço é automático na TaskMQTT. Sem RTOS, atende uma iteração.
void communication_update(communication_mode_t mode);

// Publica uma confirmacao de associacao/desassociacao no topico de confirmacoes.
// Retorna true se a mensagem foi enfileirada para envio.
bool communication_publish_confirm_assoc(const char *patient_id, bool success);
bool communication_publish_confirm_disassoc(bool success);

// Processa um comando recebido no topico de comandos.
// topic: topico da mensagem, payload: dados JSON, payload_len: tamanho em bytes.
void communication_process_command(const char *topic, const char *payload, size_t payload_len);
#endif
