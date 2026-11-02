#ifndef COMMUNICATION_H
#define COMMUNICATION_H
#include <stdbool.h>
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
void communication_update(communication_mode_t mode);
#endif
