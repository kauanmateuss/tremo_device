#ifndef TREMO_LWIPOPTS_H
#define TREMO_LWIPOPTS_H
#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_DHCP 1
#define LWIP_DNS 0
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_RAW 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define MEM_ALIGNMENT 4
#define MEM_SIZE 16384
#define MEMP_NUM_TCP_SEG 32
#define PBUF_POOL_SIZE 16
#define TCP_MSS 1460
#define TCP_WND (4 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN 32
#define MQTT_OUTPUT_RINGBUF_SIZE 2048
#define MQTT_REQ_MAX_IN_FLIGHT 1
// O único cliente MQTT agenda um timer periódico além dos timers internos do lwIP.
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 1)
#define LWIP_STATS 0
#define LWIP_CHKSUM_ALGORITHM 3
#endif
