#include "udp.h"
#include "ip.h"
#include "net.h"
#include "serial.h"
#include <stdint.h>

static uint16_t sw16(uint16_t v) { return (v >> 8) | (v << 8); }

int udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
             const void* data, uint32_t len) {
    static uint8_t pkt[2048];
    if (len + 8 > sizeof(pkt)) return -1;

    struct udp_header* udp = (struct udp_header*)pkt;
    udp->src_port = sw16(src_port);
    udp->dst_port = sw16(dst_port);
    udp->length   = sw16(8 + len);
    udp->checksum = 0;   /* Не считаем — QEMU примет */

    const uint8_t* d = (const uint8_t*)data;
    for (uint32_t i = 0; i < len; i++) pkt[8 + i] = d[i];

    return ip_send(dst_ip, 17 /* UDP */, pkt, 8 + len);
}

void udp_handle(uint32_t src_ip, const uint8_t* data, uint32_t len) {
    if (len < 8) return;
    const struct udp_header* udp = (const struct udp_header*)data;
    uint16_t sport = sw16(udp->src_port);
    uint16_t dport = sw16(udp->dst_port);

    /* DNS response приходит на src_port (наш случайный) */
    if (sport == 53 || dport == 53) {
        extern void dns_handle(const uint8_t*, uint32_t);
        dns_handle(data + 8, len - 8);
    }
    (void)src_ip;
}
