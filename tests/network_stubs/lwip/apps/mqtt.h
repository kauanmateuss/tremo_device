#include <stdint.h>
#include "lwip/ip_addr.h"
typedef int err_t;
#define ERR_OK 0
#define ERR_MEM -1
#define ERR_TIMEOUT -3
typedef struct { int connected; } mqtt_client_t;
typedef enum { MQTT_CONNECT_ACCEPTED, MQTT_CONNECT_DISCONNECTED } mqtt_connection_status_t;
typedef void (*mqtt_connection_cb_t)(mqtt_client_t *, void *, mqtt_connection_status_t);
typedef void (*mqtt_request_cb_t)(void *, err_t);
struct mqtt_connect_client_info_t { const char *client_id; uint16_t keep_alive; const char *client_user; const char *client_pass; const char *will_topic; const char *will_msg; uint8_t will_qos, will_retain; };
mqtt_client_t *mqtt_client_new(void);
void mqtt_disconnect(mqtt_client_t *);
int mqtt_client_is_connected(mqtt_client_t *);
err_t mqtt_client_connect(mqtt_client_t *, const ip_addr_t *, uint16_t, mqtt_connection_cb_t, void *, const struct mqtt_connect_client_info_t *);
err_t mqtt_publish(mqtt_client_t *, const char *, const void *, uint16_t, uint8_t, uint8_t, mqtt_request_cb_t, void *);

#define MQTT_DATA_FLAG_LAST 1
typedef void (*mqtt_incoming_publish_cb_t)(void *, const char *, uint32_t);
typedef void (*mqtt_incoming_data_cb_t)(void *, const uint8_t *, uint16_t, uint8_t);
void mqtt_set_inpub_callback(mqtt_client_t *, mqtt_incoming_publish_cb_t, mqtt_incoming_data_cb_t, void *);
err_t mqtt_subscribe(mqtt_client_t *, const char *, uint8_t, mqtt_request_cb_t, void *);
