#include "communication.h"
#include "telemetry.h"
#include "patient_assoc.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#ifdef TREMO_FREERTOS_ENABLED
#include "FreeRTOS.h"
#include "task.h"
#endif
#ifndef TREMO_MQTT_ENABLED
#define TREMO_MQTT_ENABLED 0
#endif
#if TREMO_MQTT_ENABLED
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"
#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"
#if __has_include("network_config.h")
#include "network_config.h"
#else
#include "network_config.example.h"
#endif
static mqtt_client_t *client;
static struct mqtt_connect_client_info_t info;
static ip_addr_t broker;
static char client_id[24], topic[64];
static char topic_cmd[64], topic_conf[64];
static bool ready, connecting, inflight, suspended, deferred;
static bool expecting_command;
static uint64_t retry_us, wifi_retry_us, sent_us, connect_started_us;
static uintptr_t publish_generation, connection_generation;
static unsigned backoff_s = 1;
static char sending[TELEMETRY_PAYLOAD];
static patient_assoc_t assoc;
#ifdef TREMO_FREERTOS_ENABLED
static communication_status_t cached_status = COMM_NO_RADIO;
static void task_network(void *parameter);
#endif
static void retry(void) {
    retry_us = time_us_64() + (uint64_t)backoff_s * 1000000;
    if (backoff_s < 32) backoff_s *= 2;
}
static void disconnect_client(void) {
    inflight = connecting = false;
    ++connection_generation; // Invalida callbacks de uma conexão encerrada.
    mqtt_disconnect(client);
}
static void mqtt_sub_request_cb(void *arg, err_t err);

static void connection(mqtt_client_t *c, void *arg, mqtt_connection_status_t status) {
    (void)c;
    if ((uintptr_t)arg != connection_generation) return;
    connecting = false;
    if (status == MQTT_CONNECT_ACCEPTED) {
        backoff_s = 1;
        // Inscreve no topico de comandos para este dispositivo
        snprintf(topic_cmd, sizeof(topic_cmd), "tremo/v1/devices/%s/commands", client_id);
        snprintf(topic_conf, sizeof(topic_conf), "tremo/v1/devices/%s/confirmations", client_id);
        if (mqtt_sub_unsub(client, topic_cmd, 1, mqtt_sub_request_cb, NULL, 1) != ERR_OK) {
            // Falha na inscricao, nao critica - tenta novamente na proxima reconexao
        }
    } else {
        inflight = false; retry();
    }
}
static void published(void *arg, err_t err) {
    if (!inflight || (uintptr_t)arg != publish_generation) return;
    if (err == ERR_OK) telemetry_ack(); // PUBACK do broker, não confirmação SQLite.
    inflight = false;
    if (err != ERR_OK) { disconnect_client(); retry(); }
}
// Callback chamado quando o topico de uma mensagem recebida eh conhecido.
static void mqtt_inpub_cb(void *arg, const char *topic, uint32_t topic_len) {
    (void)arg;
    if (!client) return;
    // Se o topico eh de comandos, marcamos para processar os dados que vao chegar.
    if (topic_cmd[0] != '\0' && strncmp(topic, topic_cmd, topic_len) == 0) {
        expecting_command = true;
    } else {
        expecting_command = false;
    }
}

static void mqtt_indata_cb(void *arg, const uint8_t *data, uint16_t len, u8_t flags) {
    (void)arg; (void)flags;
    if (!client || !expecting_command) return;
    char payload[256];
    uint16_t copy_len = len < sizeof(payload) - 1 ? len : sizeof(payload) - 1;
    memcpy(payload, data, copy_len);
    payload[copy_len] = '\0';
    communication_process_command(topic_cmd, payload, strlen(payload));
    expecting_command = false;
}

static void mqtt_sub_request_cb(void *arg, err_t err) {
    (void)arg;
    if (err != ERR_OK) {
        ready = false;
    }
}

static void start_network(void) {
    if (!ipaddr_aton(TREMO_MQTT_HOST, &broker) || cyw43_arch_init()) return;
    cyw43_arch_enable_sta_mode();
    client = mqtt_client_new();
    if (!client) return;
    // Configura callbacks de recebimento de mensagens
    mqtt_set_inpub_callback(client, mqtt_inpub_cb, mqtt_indata_cb, NULL);
    info = (struct mqtt_connect_client_info_t){.client_id = client_id, .keep_alive = 30,
        .client_user = TREMO_MQTT_USER[0] ? TREMO_MQTT_USER : NULL,
        .client_pass = TREMO_MQTT_USER[0] ? TREMO_MQTT_PASSWORD : NULL};
    ready = true;
}

void communication_init(void) {
    char device[17]; pico_get_unique_board_id_string(device, sizeof(device));
    for (unsigned i = 0; device[i]; ++i)
        if (device[i] >= 'A' && device[i] <= 'F') device[i] += 'a' - 'A';
    bool configured = TREMO_WIFI_SSID[0] != 0;
    telemetry_init(device, configured);
    patient_assoc_init();
    snprintf(assoc.device_id, sizeof(assoc.device_id), "%s", device);
    if (!configured) return;
    snprintf(client_id, sizeof(client_id), "tremo-%s", device);
    snprintf(topic, sizeof(topic), "tremo/v1/devices/%s/events", device);
#ifdef TREMO_FREERTOS_ENABLED
    if (xTaskCreate(task_network, "TaskMQTT", 1536, NULL, 1, NULL) != pdPASS)
        panic("FreeRTOS: falha ao criar TaskMQTT");
#else
    start_network();
#endif
}
static communication_status_t read_status(void) {
    if (!ready) return COMM_NO_RADIO;
    switch (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA)) {
    case CYW43_LINK_UP: return mqtt_client_is_connected(client) ? COMM_ONLINE : COMM_BROKER_WAIT;
    case CYW43_LINK_NONET: return COMM_WIFI_NOT_FOUND;
    case CYW43_LINK_BADAUTH: return COMM_WIFI_BAD_AUTH;
    case CYW43_LINK_NOIP: return COMM_WIFI_NO_IP;
    default: return COMM_WIFI_WAIT;
    }
}

static void service_network(communication_mode_t mode) {
    if (!ready) return;
    if (mode == COMM_SUSPENDED) {
        if (!suspended) { disconnect_client(); suspended = true; }
        return; // Nenhum poll/IRQ de rede durante testes cognitivos (arquitetura poll).
    }
    if (suspended) { suspended = false; retry_us = 0; }
    // Mesmo a associacao "async" envia comandos sincronos ao radio. Sem uma
    // conexao utilizavel, nao entrar no driver durante a aquisicao cronometrada.
    if (mode == COMM_STREAM_ONLY && (deferred || read_status() != COMM_ONLINE)) {
        deferred = true;
        return;
    }
    if (deferred) {
        // A conexao pode ter expirado enquanto o poll ficou parado. O evento
        // sem PUBACK continua na fila e sera reenviado pela nova conexao.
        disconnect_client();
        deferred = false;
        retry_us = 0;
    }
    cyw43_arch_poll();
    if (mode == COMM_STREAM_ONLY && read_status() != COMM_ONLINE) {
        deferred = true;
        return;
    }
    uint64_t now = time_us_64();
    int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (link != CYW43_LINK_UP) {
        if (now >= wifi_retry_us) {
            disconnect_client();
            cyw43_arch_wifi_connect_async(TREMO_WIFI_SSID, TREMO_WIFI_PASSWORD,
                TREMO_WIFI_PASSWORD[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
            wifi_retry_us = now + 15000000;
        }
        return;
    }
    if (!mqtt_client_is_connected(client)) {
        if (connecting && now - connect_started_us >= 15000000) {
            disconnect_client();
            retry();
            return;
        }
        if (!connecting && now >= retry_us) {
            connecting = true;
            connect_started_us = now;
            ++connection_generation;
            if (mqtt_client_connect(client, &broker, TREMO_MQTT_PORT, connection,
                                    (void *)connection_generation, &info) != ERR_OK) {
                disconnect_client();
                retry();
            }
        }
        return;
    }
    if (inflight && now - sent_us > 15000000) {
        disconnect_client(); retry(); return;
    }
    if (!inflight && telemetry_copy(sending, sizeof(sending))) {
        inflight = true;
        sent_us = now;
        ++publish_generation;
        if (mqtt_publish(client, topic, sending, strlen(sending), 1, 0,
                         published, (void *)publish_generation) != ERR_OK) {
            disconnect_client();
            retry();
        }
    }
}
#ifdef TREMO_FREERTOS_ENABLED
static void task_network(void *parameter) {
    (void)parameter;
    start_network();
    for (;;) {
        service_network(COMM_NORMAL);
        communication_status_t status = read_status();
        taskENTER_CRITICAL();
        cached_status = status;
        taskEXIT_CRITICAL();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#endif

communication_status_t communication_status(void) {
#ifdef TREMO_FREERTOS_ENABLED
    taskENTER_CRITICAL();
    communication_status_t status = cached_status;
    taskEXIT_CRITICAL();
    return status;
#else
    return read_status();
#endif
}

void communication_update(communication_mode_t mode) {
#ifdef TREMO_FREERTOS_ENABLED
    (void)mode; // A TaskMQTT atende a rede independentemente da FSM.
#else
    service_network(mode);
#endif
}

// Publica confirmacao de associacao no topico de confirmacoes.
bool communication_publish_confirm_assoc(const char *patient_id, bool success) {
#if TREMO_MQTT_ENABLED
    if (!client || !mqtt_client_is_connected(client)) return false;
    char payload[128];
    int n = snprintf(payload, sizeof(payload),
        "{\"v\":2,\"device\":\"%s\",\"confirmation\":\"association_confirmed\","
        "\"status\":\"%s\",\"data\":{\"patient_id\":\"%s\"}}",
        client_id, success ? "ok" : "error", patient_id ? patient_id : "");
    if (n < 0 || n >= (int)sizeof(payload)) return false;
    if (inflight) return false; // Ja ha uma mensagem em voo
    inflight = true;
    sent_us = time_us_64();
    ++publish_generation;
    if (mqtt_publish(client, topic_conf, payload, strlen(payload), 1, 0,
                     published, (void *)publish_generation) != ERR_OK) {
        inflight = false;
        return false;
    }
    return true;
#else
    (void)patient_id; (void)success;
    return false;
#endif
}

// Publica confirmacao de desassociacao no topico de confirmacoes.
bool communication_publish_confirm_disassoc(bool success) {
#if TREMO_MQTT_ENABLED
    if (!client || !mqtt_client_is_connected(client)) return false;
    char payload[128];
    const char *pid = patient_assoc_has_patient() ? patient_assoc_patient_id() : "";
    int n = snprintf(payload, sizeof(payload),
        "{\"v\":2,\"device\":\"%s\",\"confirmation\":\"disassociation_confirmed\","
        "\"status\":\"%s\",\"data\":{\"patient_id\":\"%s\"}}",
        client_id, success ? "ok" : "error", pid);
    if (n < 0 || n >= (int)sizeof(payload)) return false;
    if (inflight) return false;
    inflight = true;
    sent_us = time_us_64();
    ++publish_generation;
    if (mqtt_publish(client, topic_conf, payload, strlen(payload), 1, 0,
                     published, (void *)publish_generation) != ERR_OK) {
        inflight = false;
        return false;
    }
    return true;
#else
    (void)success;
    return false;
#endif
}

// Processa um comando recebido no topico de comandos.
// Espera JSON no formato: {"v":2,"device":"...","command":"associate_patient","data":{"patient_id":"..."}}
void communication_process_command(const char *topic, const char *payload, size_t payload_len) {
#if TREMO_MQTT_ENABLED
    (void)topic;
    if (!payload || payload_len == 0 || payload_len > 255) return;

    // Parsing simples do JSON do comando
    char cmd_buf[64] = {0};
    char patient_buf[PATIENT_ID_MAX + 1] = {0};

    // Extrai o comando
    const char *cmd_key = "\"command\":\"";
    const char *cmd_pos = strstr(payload, cmd_key);
    if (cmd_pos) {
        cmd_pos += strlen(cmd_key);
        size_t i = 0;
        while (*cmd_pos && *cmd_pos != '"' && i < sizeof(cmd_buf) - 1) {
            cmd_buf[i++] = *cmd_pos++;
        }
        cmd_buf[i] = '\0';
    }

    // Extrai o patient_id do data
    const char *pid_key = "\"patient_id\":\"";
    const char *pid_pos = strstr(payload, pid_key);
    if (pid_pos) {
        pid_pos += strlen(pid_key);
        size_t i = 0;
        while (*pid_pos && *pid_pos != '"' && i < sizeof(patient_buf) - 1) {
            patient_buf[i++] = *pid_pos++;
        }
        patient_buf[i] = '\0';
    }

    if (strcmp(cmd_buf, "associate_patient") == 0) {
        if (patient_assoc_request_associate(patient_buf[0] ? patient_buf : NULL)) {
            communication_publish_confirm_assoc(patient_buf, true);
        } else {
            communication_publish_confirm_assoc(NULL, false);
        }
    } else if (strcmp(cmd_buf, "disassociate_patient") == 0) {
        if (patient_assoc_request_disassociate()) {
            communication_publish_confirm_disassoc(true);
        } else {
            communication_publish_confirm_disassoc(false);
        }
    }
#else
    (void)payload; (void)payload_len;
#endif
}

#else
void communication_init(void) { telemetry_init("0000000000000000", false); }
communication_status_t communication_status(void) { return COMM_NO_RADIO; }
void communication_update(communication_mode_t mode) { (void)mode; }
#endif
