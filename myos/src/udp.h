#ifndef UDP_H
#define UDP_H
#include <stdint.h>

struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

int  udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
              const void* data, uint32_t len);
void udp_handle(uint32_t src_ip, const uint8_t* data, uint32_t len);

#endif
