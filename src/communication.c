#include "communication.h"
#include "telemetry.h"
#include "patient_assoc.h"
#include "session.h"
#ifndef PICO_PROGRAM_VERSION_STRING
#define PICO_PROGRAM_VERSION_STRING "0.1"
#endif
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#ifdef TREMO_FREERTOS_ENABLED
#include "FreeRTOS.h"
#include "task.h"
#endif
#ifdef PICO_ON_DEVICE
#include "pico/unique_id.h"
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
static char topic_cmd[64], topic_conf[64], offline_notice[160];
static bool ready, connecting, inflight, suspended, deferred;
static bool expecting_command;
static uint64_t retry_us, wifi_retry_us, sent_us, connect_started_us;
static uintptr_t publish_generation, connection_generation;
static unsigned backoff_s = 1;
static char sending[TELEMETRY_PAYLOAD];
static char command_payload[192];
static size_t command_length, command_expected;
static bool response_due;

// TaskMQTT recebe o pedido; apenas a task dona da sessão altera a configuração.
#ifdef TREMO_FREERTOS_ENABLED
#define MPU_LOCK() taskENTER_CRITICAL()
#define MPU_UNLOCK() taskEXIT_CRITICAL()
#else
#define MPU_LOCK() ((void)0)
#define MPU_UNLOCK() ((void)0)
#endif
static enum { MPU_IDLE, MPU_PENDING, MPU_REPLY } mpu_state;
static struct {
    char id[17];
    unsigned sample_ms, duration_ms;
    bool ok;
} mpu_request;

static bool reply_mpu(const char *id, bool ok) {
    char payload[160];
    int n = snprintf(payload, sizeof(payload),
        "{\"v\":2,\"device\":\"%s\",\"confirmation\":\"mpu_configured\","
        "\"status\":\"%s\",\"data\":{\"request_id\":\"%s\"}}",
        patient_assoc_device_id(), ok ? "ok" : "error", id);
    return n > 0 && (size_t)n < sizeof(payload) &&
        mqtt_publish(client, topic_conf, payload, n, 1, 0, NULL, NULL) == ERR_OK;
}

static bool receive_mpu_request(void) {
    char device[17], id[17];
    unsigned sample, duration;
    int consumed = 0;
    int fields = sscanf(command_payload,
        "{\"v\":2,\"device\":\"%16[0-9a-f]\",\"command\":\"configure_mpu\","
        "\"data\":{\"request_id\":\"%16[0-9a-f]\",\"sample_ms\":%2u,\"duration_ms\":%5u}}%n",
        device, id, &sample, &duration, &consumed);
    if (fields != 4 || (size_t)consumed != command_length || strlen(id) != 16 ||
        strcmp(device, patient_assoc_device_id())) return false;
    MPU_LOCK();
    bool busy = mpu_state != MPU_IDLE;
    bool duplicate = busy && !strcmp(mpu_request.id, id);
    if (!busy) {
        strcpy(mpu_request.id, id);
        mpu_request.sample_ms = sample;
        mpu_request.duration_ms = duration;
        mpu_state = MPU_PENDING;
    }
    MPU_UNLOCK();
    if (busy && !duplicate) reply_mpu(id, false);
    return true;
}

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
static void incoming_topic(void *arg, const char *name, uint32_t total);
static void incoming_data(void *arg, const uint8_t *data, uint16_t len, uint8_t flags);

static void connection(mqtt_client_t *c, void *arg, mqtt_connection_status_t status) {
    (void)c;
    if ((uintptr_t)arg != connection_generation) return;
    connecting = false;
    if (status == MQTT_CONNECT_ACCEPTED) {
        backoff_s = 1;
        mqtt_set_inpub_callback(client, incoming_topic, incoming_data, NULL);
        if (mqtt_subscribe(client, topic_cmd, 1, mqtt_sub_request_cb, arg) != ERR_OK) {
            disconnect_client();
            retry();
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
static bool publish_response(const char *type, const char *patient, bool ok, bool retain) {
    char response[192];
    int n = snprintf(response, sizeof(response),
        "{\"v\":2,\"device\":\"%s\",\"confirmation\":\"%s\",\"status\":\"%s\","
        "\"data\":{\"patient_id\":\"%s\",\"firmware\":\"" PICO_PROGRAM_VERSION_STRING "\"}}",
        patient_assoc_device_id(), type, ok ? "ok" : "error", patient);
    return n > 0 && (size_t)n < sizeof(response) &&
        mqtt_publish(client, topic_conf, response, n, 1, retain, NULL, NULL) == ERR_OK;
}

static void incoming_topic(void *arg, const char *name, uint32_t total) {
    (void)arg;
    command_length = 0;
    command_expected = total;
    expecting_command = !strcmp(name, topic_cmd) && total && total < sizeof(command_payload);
}

static void incoming_data(void *arg, const uint8_t *data, uint16_t len, uint8_t flags) {
    (void)arg;
    if (!expecting_command) return;
    if (command_length + len > command_expected) { expecting_command = false; return; }
    memcpy(command_payload + command_length, data, len);
    command_length += len;
    if (!(flags & MQTT_DATA_FLAG_LAST)) return;
    expecting_command = false;
    if (command_length != command_expected) return;
    command_payload[command_length] = 0;
    if (receive_mpu_request()) return;
    char device[17], action[32], patient[17];
    int consumed = 0;
    // Formato compacto emitido por encode_command. Verifica todos os campos antes da alteração.
    int fields = sscanf(command_payload,
        "{\"v\":2,\"device\":\"%16[0-9a-f]\",\"command\":\"%31[a-z_]\","
        "\"data\":{\"patient_id\":\"%16[a-zA-Z0-9_-]\"}}%n", device, action, patient, &consumed);
    if (fields != 3 || (size_t)consumed != command_length || strcmp(device, patient_assoc_device_id())) return;
    if (!strcmp(action, "associate_patient")) {
        if (publish_response("association_confirmed", patient, true, false)) {
            patient_assoc_set(patient);
            response_due = true;
        }
    } else if (!strcmp(action, "disassociate_patient")) {
        char current[PATIENT_ID_MAX + 1];
        patient_assoc_copy(current);
        bool ok = !current[0] || !strcmp(current, patient);
        if (publish_response("disassociation_confirmed", patient, ok, false) && ok) {
            patient_assoc_set("");
            response_due = true;
        }
    }
}

static void mqtt_sub_request_cb(void *arg, err_t err) {
    if ((uintptr_t)arg != connection_generation) return;
    if (err != ERR_OK) { disconnect_client(); retry(); return; }
    response_due = true;
}

static void start_network(void) {
    if (!ipaddr_aton(TREMO_MQTT_HOST, &broker) || cyw43_arch_init()) return;
    cyw43_arch_enable_sta_mode();
    client = mqtt_client_new();
    if (!client) return;
    snprintf(offline_notice, sizeof(offline_notice),
        "{\"v\":2,\"device\":\"%s\",\"confirmation\":\"device_state\","
        "\"status\":\"offline\",\"data\":{\"patient_id\":\"\"}}", patient_assoc_device_id());
    info = (struct mqtt_connect_client_info_t){.will_topic = topic_conf, .will_msg = offline_notice,
        .will_qos = 1, .will_retain = 1, .client_id = client_id, .keep_alive = 30,
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

    patient_assoc_set_device_id(device);
    snprintf(topic_cmd, sizeof(topic_cmd), "tremo/v1/devices/%s/commands", device);
    snprintf(topic_conf, sizeof(topic_conf), "tremo/v1/devices/%s/confirmations", device);
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
    if (response_due) {
        char patient[PATIENT_ID_MAX + 1];
        patient_assoc_copy(patient);
        if (publish_response("device_state", patient, true, true)) response_due = false;
    }
    MPU_LOCK();
    bool reply_ready = mpu_state == MPU_REPLY;
    MPU_UNLOCK();
    if (reply_ready && reply_mpu(mpu_request.id, mpu_request.ok)) {
        MPU_LOCK();
        mpu_state = MPU_IDLE;
        MPU_UNLOCK();
    }
    if (inflight && now - sent_us > 15000000) {
        disconnect_client(); retry(); return;
    }
    if (!inflight && telemetry_copy(sending, sizeof(sending))) {
        inflight = true;
        sent_us = now;
        ++publish_generation;
        err_t result = mqtt_publish(client, topic, sending, strlen(sending), 1, 0,
                                    published, (void *)publish_generation);
        if (result == ERR_MEM) {
            inflight = false; // Aguarda espaço sem remover o evento nem derrubar a conexão.
        } else if (result != ERR_OK) {
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
    MPU_LOCK();
    if (mpu_state == MPU_PENDING) {
        session_config_t config = *session_config();
        config.sample_ms = mpu_request.sample_ms;
        config.duration_ms = mpu_request.duration_ms;
        mpu_request.ok = session_configure(config);
        mpu_state = MPU_REPLY;
    }
    MPU_UNLOCK();
#ifdef TREMO_FREERTOS_ENABLED
    (void)mode; // A TaskMQTT atende a rede independentemente da FSM.
#else
    service_network(mode);
#endif
}

#else
void communication_init(void) {
#ifdef PICO_ON_DEVICE
    char device[17];
    pico_get_unique_board_id_string(device, sizeof(device));
    patient_assoc_set_device_id(device);
#endif
    telemetry_init(patient_assoc_device_id(), false);
}
communication_status_t communication_status(void) { return COMM_NO_RADIO; }
void communication_update(communication_mode_t mode) { (void)mode; }
#endif
