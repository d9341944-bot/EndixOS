#ifndef RTL8139_H
#define RTL8139_H
#include <stdint.h>

int  rtl8139_init(void);
void rtl8139_get_mac(uint8_t* mac);
int  rtl8139_send(const void* data, uint32_t len);
int  rtl8139_recv(void* buf, uint32_t max);
int  rtl8139_ready(void);

#endif
