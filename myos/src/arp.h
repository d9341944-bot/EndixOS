#ifndef ARP_H
#define ARP_H
#include <stdint.h>

struct arp_entry {
    uint32_t ip;
    uint8_t  mac[6];
    int      valid;
};

void arp_send_request(uint32_t target_ip);
void arp_handle(const uint8_t* data, uint32_t len);
int  arp_lookup(uint32_t ip, uint8_t* out_mac);

#endif
