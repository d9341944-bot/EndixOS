#ifndef IP_H
#define IP_H
#include <stdint.h>

/* IPv4 header */
struct ip_header {
    uint8_t  ver_ihl;      /* version(4) + ihl(4) */
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed));

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

uint16_t ip_checksum(const void* data, uint32_t len);
int      ip_send(uint32_t dst_ip, uint8_t protocol, const void* data, uint32_t len);
void     ip_handle(const uint8_t* data, uint32_t len);

#endif
