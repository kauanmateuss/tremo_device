#include <stdint.h>
#define CYW43_ITF_STA 0
#define CYW43_LINK_UP 3
#define CYW43_LINK_NOIP 2
#define CYW43_LINK_NONET (-2)
#define CYW43_LINK_BADAUTH (-3)
#define CYW43_AUTH_WPA2_AES_PSK 1
#define CYW43_AUTH_OPEN 0
extern int cyw43_state;
int cyw43_arch_init(void);
void cyw43_arch_enable_sta_mode(void);
void cyw43_arch_poll(void);
int cyw43_tcpip_link_status(int *, int);
int cyw43_arch_wifi_connect_async(const char *, const char *, uint32_t);
