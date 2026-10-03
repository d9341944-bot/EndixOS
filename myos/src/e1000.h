#ifndef E1000_H
#define E1000_H
#include <stdint.h>

int  e1000_init(void);
int  e1000_ready(void);
void e1000_get_mac(uint8_t* mac);
int  e1000_send(const void* data, uint32_t len);
int  e1000_recv(void* buf, uint32_t max);

#endif
