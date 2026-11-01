#ifndef TEST_IP_ADDR
#define TEST_IP_ADDR
typedef struct { unsigned value; } ip_addr_t;
int ipaddr_aton(const char *, ip_addr_t *);
#endif
