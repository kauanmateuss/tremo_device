#include "communication.h"
#include "telemetry.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
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
static bool ready, connecting, inflight, suspended;
static uint64_t retry_us, wifi_retry_us, sent_us;
static unsigned backoff_s = 1;
static void retry(void) {
    retry_us = time_us_64() + (uint64_t)backoff_s * 1000000;
    if (backoff_s < 32) backoff_s *= 2;
}
static void connection(mqtt_client_t *c, void *arg, mqtt_connection_status_t status) {
    (void)c; (void)arg; connecting = false;
    if (status == MQTT_CONNECT_ACCEPTED) { backoff_s = 1; printf("# MQTT conectado\n"); }
    else { inflight = false; retry(); printf("# MQTT desconectado status=%u\n", status); }
}
static void published(void *arg, err_t err) {
    (void)arg;
    if (err == ERR_OK) telemetry_ack(); // PUBACK do broker, não confirmação SQLite.
    inflight = false;
    if (err != ERR_OK) { mqtt_disconnect(client); connecting = false; retry(); }
}
void communication_init(void) {
    char device[17]; pico_get_unique_board_id_string(device, sizeof(device));
    // O SDK usa A..F; o contrato JSON e o tópico usam hexadecimal minúsculo.
    for (unsigned i = 0; device[i]; ++i)
        if (device[i] >= 'A' && device[i] <= 'F') device[i] += 'a' - 'A';
    bool configured = TREMO_WIFI_SSID[0] != 0;
    telemetry_init(device, configured);
    if (!configured) { printf("# MQTT: preencher config/network_config.h\n"); return; }
    if (!ipaddr_aton(TREMO_MQTT_HOST, &broker) || cyw43_arch_init()) { printf("# MQTT: falha ao iniciar rede\n"); return; }
    cyw43_arch_enable_sta_mode();
    client = mqtt_client_new();
    if (!client) { printf("# MQTT: sem memoria\n"); return; }
    snprintf(client_id, sizeof(client_id), "tremo-%s", device);
    snprintf(topic, sizeof(topic), "tremo/v1/devices/%s/events", device);
    info = (struct mqtt_connect_client_info_t){.client_id = client_id, .keep_alive = 30,
        .client_user = TREMO_MQTT_USER[0] ? TREMO_MQTT_USER : NULL,
        .client_pass = TREMO_MQTT_USER[0] ? TREMO_MQTT_PASSWORD : NULL};
    ready = true;
}
void communication_update(bool allowed) {
    if (!ready) return;
    if (!allowed) {
        if (!suspended) { mqtt_disconnect(client); inflight = connecting = false; suspended = true; }
        return; // Nenhum poll/IRQ de rede durante testes cognitivos (arquitetura poll).
    }
    if (suspended) { suspended = false; retry_us = 0; }
    cyw43_arch_poll();
    uint64_t now = time_us_64();
    int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (link != CYW43_LINK_UP) {
        if (now >= wifi_retry_us) {
            mqtt_disconnect(client); inflight = connecting = false;
            printf("# WIFI tentativa link=%d\n", link);
            int err = cyw43_arch_wifi_connect_async(TREMO_WIFI_SSID, TREMO_WIFI_PASSWORD,
                TREMO_WIFI_PASSWORD[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
            if (err) printf("# WIFI erro ao iniciar tentativa=%d\n", err);
            wifi_retry_us = now + 15000000;
        }
        return;
    }
    if (!mqtt_client_is_connected(client)) {
        if (!connecting && now >= retry_us) {
            connecting = true;
            if (mqtt_client_connect(client, &broker, TREMO_MQTT_PORT, connection, NULL, &info) != ERR_OK) { connecting = false; retry(); }
        }
        return;
    }
    if (inflight && now - sent_us > 15000000) {
        mqtt_disconnect(client); inflight = connecting = false; retry(); return;
    }
    const char *payload = telemetry_peek();
    if (!inflight && payload) {
        inflight = true; sent_us = now;
        if (mqtt_publish(client, topic, payload, strlen(payload), 1, 0, published, NULL) != ERR_OK) inflight = false;
    }
}
#else
void communication_init(void) { telemetry_init("0000000000000000", false); }
void communication_update(bool allowed) { (void)allowed; }
#endif
