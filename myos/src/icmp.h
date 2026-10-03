#ifndef ICMP_H
#define ICMP_H
#include <stdint.h>

void icmp_send_ping(uint32_t dst_ip);
void icmp_handle(const uint8_t* data, uint32_t len);
int  icmp_last_reply_ms(void);

#endif
