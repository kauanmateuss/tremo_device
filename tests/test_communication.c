#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "communication.h"
#include "patient_assoc.h"
#include "telemetry.h"
#include "lwip/apps/mqtt.h"
#include "pico/cyw43_arch.h"
static mqtt_incoming_publish_cb_t incoming_topic;
static mqtt_incoming_data_cb_t incoming_data;
static char confirmation[192];
static void send_command(const char *action, const char *patient) {
    char json[192];
    int len = snprintf(json, sizeof(json),
        "{\"v\":2,\"device\":\"abcdef0123456789\",\"command\":\"%s\",\"data\":{\"patient_id\":\"%s\"}}", action, patient);
    incoming_topic(NULL, "tremo/v1/devices/abcdef0123456789/commands", len);
    incoming_data(NULL, (const uint8_t *)json, 9, 0);
    incoming_data(NULL, (const uint8_t *)json + 9, len - 9, MQTT_DATA_FLAG_LAST);
}
static void configure_mpu(void) {
    const char json[] = "{\"v\":2,\"device\":\"abcdef0123456789\",\"command\":\"configure_mpu\","
        "\"data\":{\"request_id\":\"0000000000000001\",\"sample_ms\":20,\"duration_ms\":60000}}";
    incoming_topic(NULL, "tremo/v1/devices/abcdef0123456789/commands", strlen(json));
    incoming_data(NULL, (const uint8_t *)json, strlen(json), MQTT_DATA_FLAG_LAST);
    communication_update(COMM_NORMAL);
    assert(strstr(confirmation, "mpu_configured") && strstr(confirmation, "0000000000000001"));
}
static uint64_t now;
static mqtt_client_t network;
static mqtt_connection_cb_t connect_cb;
static mqtt_request_cb_t publish_cb;
static void *publish_arg, *connect_arg;
static unsigned joins, polls, connects, publishes, disconnects;
static int link = CYW43_LINK_UP;
static err_t publish_error;
static char previous[TELEMETRY_PAYLOAD];
int cyw43_state;
uint64_t time_us_64(void) { return now; }
int cyw43_arch_init(void) { return 0; }
void cyw43_arch_enable_sta_mode(void) {}
void cyw43_arch_poll(void) { ++polls; }
int cyw43_tcpip_link_status(int *s, int itf) { (void)s; (void)itf; return link; }
int cyw43_arch_wifi_connect_async(const char *ssid, const char *password, uint32_t auth) { (void)ssid; (void)password; (void)auth; ++joins; return 0; }
void pico_get_unique_board_id_string(char *out, unsigned n) { snprintf(out, n, "ABCDEF0123456789"); }
int ipaddr_aton(const char *host, ip_addr_t *ip) { (void)ip; return !strcmp(host, "192.168.1.215"); }
mqtt_client_t *mqtt_client_new(void) { return &network; }
void mqtt_disconnect(mqtt_client_t *c) { c->connected = 0; publish_cb = NULL; ++disconnects; }
int mqtt_client_is_connected(mqtt_client_t *c) { return c->connected; }
err_t mqtt_client_connect(mqtt_client_t *c, const ip_addr_t *ip, uint16_t port, mqtt_connection_cb_t cb, void *arg, const struct mqtt_connect_client_info_t *info) {
    (void)c; (void)ip; (void)arg;
    assert(port == 1883 && !strcmp(info->client_id, "tremo-abcdef0123456789"));
    connect_cb = cb; connect_arg = arg; ++connects; return ERR_OK;
}
err_t mqtt_publish(mqtt_client_t *c, const char *topic, const void *payload, uint16_t len, uint8_t qos, uint8_t retain, mqtt_request_cb_t cb, void *arg) {
    (void)c; (void)arg;
    if (strstr(topic, "/confirmations")) {
        assert(!cb && qos == 1 && len < sizeof(confirmation));
        memcpy(confirmation, payload, len);
        confirmation[len] = 0;
        return ERR_OK;
    }
    assert(qos == 1 && retain == 0);
    assert(!strcmp(topic, "tremo/v1/devices/abcdef0123456789/events"));
    assert(len == strlen(payload)); strcpy(previous, payload);
    ++publishes; if (!publish_error) { publish_cb = cb; publish_arg = arg; } return publish_error;
}
static void connected(void) { network.connected = 1; connect_cb(&network, connect_arg, MQTT_CONNECT_ACCEPTED); }
int main(void) {
    communication_init(); session_init(1); assert(session_begin(SESSION_MOT, 0));
    link = CYW43_LINK_NOIP;
    for (unsigned i = 0; i < 4; ++i) {
        now += 15000000;
        communication_update(COMM_NORMAL);
        assert(communication_status() == COMM_WIFI_NO_IP && !joins && !connects);
    }
    link = CYW43_LINK_UP;
    communication_update(COMM_NORMAL); assert(connects == 1); connected();
    communication_update(COMM_NORMAL); assert(publishes == 1 && telemetry_pending() == 1);
    assert(strstr(previous, "abcdef0123456789"));
    communication_update(COMM_NORMAL); assert(publishes == 1); // Aguarda PUBACK.
    char patient[PATIENT_ID_MAX + 1];
    send_command("associate_patient", "PAC-1");
    patient_assoc_copy(patient);
    assert(!strcmp(patient, "PAC-1") && strstr(confirmation, "association_confirmed"));
    send_command("associate_patient", "PAC-2");
    send_command("disassociate_patient", "PAC-1");
    patient_assoc_copy(patient);
    assert(!strcmp(patient, "PAC-2") && strstr(confirmation, "error"));
    send_command("disassociate_patient", "PAC-2");
    patient_assoc_copy(patient);
    assert(!patient[0] && strstr(confirmation, "disassociation_confirmed"));
    assert(telemetry_pending() == 1);
    unsigned before = polls;
    communication_update(COMM_SUSPENDED); communication_update(COMM_SUSPENDED);
    assert(polls == before && disconnects == 1 && telemetry_pending() == 1);
    communication_update(COMM_NORMAL); assert(connects == 2); connected();
    communication_update(COMM_NORMAL); assert(publishes == 2 && !strcmp(previous, telemetry_peek()));
    mqtt_request_cb_t stale_ack = publish_cb;
    void *stale_arg = publish_arg;
    publish_cb(publish_arg, ERR_OK); assert(telemetry_pending() == 0);
    session_finish(SESSION_COMPLETE, 1); publish_error = ERR_MEM;
    communication_update(COMM_NORMAL);
    assert(network.connected && telemetry_pending() == 1);
    publish_error = ERR_TIMEOUT;
    communication_update(COMM_NORMAL); assert(telemetry_pending() == 1);
    publish_error = ERR_OK;
    before = connects;
    communication_update(COMM_NORMAL); assert(connects == before);
    now += 1000000; communication_update(COMM_NORMAL); connected();
    communication_update(COMM_NORMAL);
    stale_ack(stale_arg, ERR_OK); // Confirmação duplicada do envio anterior.
    assert(telemetry_pending() == 1);
    now += 16000000; communication_update(COMM_NORMAL); // Timeout conserva a mensagem.
    assert(!network.connected && telemetry_pending() == 1);
    before = connects; communication_update(COMM_NORMAL); assert(connects == before);
    now += 1000000; communication_update(COMM_NORMAL); assert(connects == before + 1); connected();
    communication_update(COMM_NORMAL); publish_cb(publish_arg, ERR_OK); assert(!telemetry_pending());
    // Sessão com rede saudável: publica normalmente. Se o link cair quando
    // há um envio pendente, a aquisição segue sem reiniciar o rádio.
    assert(session_begin(SESSION_TREMOR, now));
    communication_update(COMM_STREAM_ONLY);
    assert(communication_status() == COMM_ONLINE && publish_cb && telemetry_pending() == 1);
    link = 0;
    unsigned old_polls = polls, old_connects = connects;
    for (unsigned i = 0; i < 61; ++i) {
        now += 1000000;
        communication_update(COMM_STREAM_ONLY);
    }
    assert(communication_status() != COMM_ONLINE && polls == old_polls && connects == old_connects && !joins);
    assert(telemetry_pending() == 1);
    communication_update(COMM_NORMAL); assert(joins == 1 && !publish_cb);
    link = CYW43_LINK_UP;
    communication_update(COMM_NORMAL); assert(connects == old_connects + 1); connected();
    communication_update(COMM_NORMAL); assert(publish_cb && telemetry_pending() == 1);
    publish_cb(publish_arg, ERR_OK); assert(!telemetry_pending());
    session_finish(SESSION_COMPLETE, now);
    communication_update(COMM_SUSPENDED);
    communication_update(COMM_NORMAL);
    void *old_connection = connect_arg;
    mqtt_connection_cb_t old_callback = connect_cb;
    now += 16000000;
    before = disconnects;
    communication_update(COMM_NORMAL);
    assert(disconnects > before && telemetry_pending() == 1);
    now += 1000000;
    communication_update(COMM_NORMAL);
    old_callback(&network, old_connection, MQTT_CONNECT_DISCONNECTED);
    connected();
    communication_update(COMM_NORMAL);
    assert(publish_cb && telemetry_pending() == 1);
    publish_cb(publish_arg, ERR_OK);
    assert(!telemetry_pending());
    configure_mpu();
    assert(strstr(confirmation, "ok") && session_config()->sample_ms == 20 && session_config()->duration_ms == 60000);
    assert(session_begin(SESSION_MOT, now));
    configure_mpu();
    assert(strstr(confirmation, "error"));
    puts("PASS MQTT: ID, QoS, PUBACK, pausa, reenvio, memoria, timeout e reconexao");
    return 0;
}

void mqtt_set_inpub_callback(mqtt_client_t *c, mqtt_incoming_publish_cb_t topic_cb, mqtt_incoming_data_cb_t data_cb, void *arg) {
    (void)c; (void)arg; incoming_topic = topic_cb; incoming_data = data_cb;
}
err_t mqtt_subscribe(mqtt_client_t *c, const char *topic, uint8_t qos, mqtt_request_cb_t cb, void *arg) {
    (void)c; cb(arg, ERR_OK);
    assert(strstr(topic, "/abcdef0123456789/commands") && qos == 1);
    return ERR_OK;
}
