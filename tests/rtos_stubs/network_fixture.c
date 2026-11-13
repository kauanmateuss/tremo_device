#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "communication.h"
#include "telemetry.h"
#include "session.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "FreeRTOS.h"
#include "task.h"

static mqtt_client_t client;
static mqtt_connection_cb_t connection_cb;
static mqtt_request_cb_t request_cb;
static void *connection_arg, *request_arg;
int mock_link = CYW43_LINK_UP;
unsigned mock_joins, mock_polls, mock_publications;
uint64_t mock_offset_us;
int cyw43_state;

static void network_owner(void) {
    assert(strcmp(pcTaskGetName(NULL), "TaskMQTT") == 0);
}
uint64_t time_us_64(void) { return (uint64_t)xTaskGetTickCount() * 1000 + mock_offset_us; }
int cyw43_arch_init(void) { network_owner(); vTaskDelay(100); return 0; }
void cyw43_arch_enable_sta_mode(void) { network_owner(); }
int cyw43_tcpip_link_status(int *state, int itf) {
    (void)state; (void)itf; network_owner(); return mock_link;
}
void cyw43_arch_poll(void) {
    network_owner(); ++mock_polls;
    if (connection_cb && mock_link == CYW43_LINK_UP) {
        mqtt_connection_cb_t callback = connection_cb;
        connection_cb = NULL; client.connected = 1;
        callback(&client, connection_arg, MQTT_CONNECT_ACCEPTED);
    }
    if (request_cb && mock_link == CYW43_LINK_UP) {
        mqtt_request_cb_t callback = request_cb;
        request_cb = NULL; callback(request_arg, ERR_OK);
    }
}
int cyw43_arch_wifi_connect_async(const char *ssid, const char *password, uint32_t auth) {
    (void)ssid; (void)password; (void)auth; network_owner(); ++mock_joins; vTaskDelay(50); return 0;
}
void pico_get_unique_board_id_string(char *out, unsigned length) {
    snprintf(out, length, "ABCDEF0123456789");
}
int ipaddr_aton(const char *host, ip_addr_t *address) {
    (void)host; (void)address; network_owner(); return 1;
}
mqtt_client_t *mqtt_client_new(void) { network_owner(); return &client; }
void mqtt_disconnect(mqtt_client_t *value) {
    network_owner(); value->connected = 0; connection_cb = NULL; request_cb = NULL;
}
int mqtt_client_is_connected(mqtt_client_t *value) { network_owner(); return value->connected; }
err_t mqtt_client_connect(mqtt_client_t *value, const ip_addr_t *address, uint16_t port,
                         mqtt_connection_cb_t callback, void *arg, const struct mqtt_connect_client_info_t *info) {
    (void)value; (void)address; (void)port; (void)info; network_owner();
    connection_cb = callback; connection_arg = arg; return ERR_OK;
}
err_t mqtt_publish(mqtt_client_t *value, const char *topic, const void *payload,
                   uint16_t length, uint8_t qos, uint8_t retain, mqtt_request_cb_t callback, void *arg) {
    (void)value; network_owner();
    assert(qos == 1 && retain == 0 && length == strlen(payload));
    assert(strstr(topic, "abcdef0123456789") && strstr(payload, "\"session\""));
    request_cb = callback; request_arg = arg; ++mock_publications; return ERR_OK;
}
void panic(const char *format, ...) { (void)format; abort(); }
void rtos_assert_failed(const char *file, int line) {
    fprintf(stderr, "RTOS assert %s:%d\n", file, line); abort();
}
void vApplicationMallocFailedHook(void) { abort(); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task; (void)name; abort();
}
