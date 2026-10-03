#ifndef DNS_H
#define DNS_H
#include <stdint.h>

void     dns_init(void);
int      dns_lookup(const char* name, uint32_t* out_ip);  /* -1 если ждём ответа */
uint32_t dns_last_ip(void);
int      dns_pending(void);
const char* dns_last_name(void);
void     dns_handle(const uint8_t* data, uint32_t len);

#endif
