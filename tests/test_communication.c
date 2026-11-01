#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "communication.h"
#include "telemetry.h"
#include "lwip/apps/mqtt.h"
#include "pico/cyw43_arch.h"
static uint64_t now;
static mqtt_client_t network;
static mqtt_connection_cb_t connect_cb;
static mqtt_request_cb_t publish_cb;
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
    connect_cb = cb; ++connects; return ERR_OK;
}
err_t mqtt_publish(mqtt_client_t *c, const char *topic, const void *payload, uint16_t len, uint8_t qos, uint8_t retain, mqtt_request_cb_t cb, void *arg) {
    (void)c; (void)arg; assert(qos == 1 && retain == 0);
    assert(!strcmp(topic, "tremo/v1/devices/abcdef0123456789/events"));
    assert(len == strlen(payload)); strcpy(previous, payload);
    ++publishes; if (!publish_error) publish_cb = cb; return publish_error;
}
static void connected(void) { network.connected = 1; connect_cb(&network, NULL, MQTT_CONNECT_ACCEPTED); }
int main(void) {
    communication_init(); session_init(1); assert(session_begin(SESSION_MOT, 0));
    communication_update(COMM_NORMAL); assert(connects == 1); connected();
    communication_update(COMM_NORMAL); assert(publishes == 1 && telemetry_pending() == 1);
    assert(strstr(previous, "abcdef0123456789"));
    communication_update(COMM_NORMAL); assert(publishes == 1); // Aguarda PUBACK.
    unsigned before = polls;
    communication_update(COMM_SUSPENDED); communication_update(COMM_SUSPENDED);
    assert(polls == before && disconnects == 1 && telemetry_pending() == 1);
    communication_update(COMM_NORMAL); assert(connects == 2); connected();
    communication_update(COMM_NORMAL); assert(publishes == 2 && !strcmp(previous, telemetry_peek()));
    publish_cb(NULL, ERR_OK); assert(telemetry_pending() == 0);
    session_finish(SESSION_COMPLETE, 1); publish_error = ERR_MEM;
    communication_update(COMM_NORMAL); assert(telemetry_pending() == 1);
    publish_error = ERR_OK; communication_update(COMM_NORMAL);
    assert(telemetry_pending() == 1);
    now += 16000000; communication_update(COMM_NORMAL); // Timeout conserva a mensagem.
    assert(!network.connected && telemetry_pending() == 1);
    before = connects; communication_update(COMM_NORMAL); assert(connects == before);
    now += 1000000; communication_update(COMM_NORMAL); assert(connects == before + 1); connected();
    communication_update(COMM_NORMAL); publish_cb(NULL, ERR_OK); assert(!telemetry_pending());
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
    publish_cb(NULL, ERR_OK); assert(!telemetry_pending());
    puts("PASS MQTT: ID, QoS, PUBACK, pausa, reenvio, memoria, timeout e reconexao");
    return 0;
}
