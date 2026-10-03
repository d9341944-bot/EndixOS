#ifndef NET_H
#define NET_H
#include <stdint.h>

void     net_init(void);
void     net_poll(void);
int      net_ready(void);
void     net_get_mac(uint8_t* out);
uint32_t net_ip(void);
void     net_set_ip(uint32_t ip);

/* Отправить Ethernet-фрейм */
int      net_send_eth(const uint8_t* dst_mac, uint16_t ethertype,
                      const void* data, uint32_t len);

#endif
