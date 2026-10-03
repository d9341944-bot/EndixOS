#include "ip.h"
#include "net.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "serial.h"
#include <stdint.h>

static uint16_t sw16(uint16_t v) { return (v >> 8) | (v << 8); }
static uint32_t sw32(uint32_t v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}

uint16_t ip_checksum(const void* data, uint32_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < len; i += 2)
        sum += ((uint16_t)p[i] << 8) | p[i+1];
    if (len & 1) sum += ((uint16_t)p[len-1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum & 0xFFFF;
}

#define GATEWAY_IP 0x0A000202u   /* 10.0.2.2 */

/* Убедиться что ARP для шлюза в кэше */
void ip_prime_gateway(void) {
    uint8_t mac[6];
    if (arp_lookup(GATEWAY_IP, mac) != 0) {
        arp_send_request(GATEWAY_IP);
    }
}

int ip_send(uint32_t dst_ip, uint8_t protocol, const void* data, uint32_t len) {
    /* Ищем MAC для dst_ip через ARP */
    uint8_t dst_mac[6];
    if (arp_lookup(dst_ip, dst_mac) != 0) {
        /* Нет в кэше для dst — пробуем MAC шлюза.
           QEMU SLIRP отвечает на ARP только для 10.0.2.2, а для остальных
           адресов (включая 10.0.2.3 DNS) использует тот же MAC. */
        if (arp_lookup(GATEWAY_IP, dst_mac) != 0) {
            /* Даже шлюза нет — просим ARP и откладываем */
            arp_send_request(GATEWAY_IP);
            return -1;
        }
        /* Используем MAC шлюза — QEMU доставит по IP-адресу */
    }

    static uint8_t pkt[2048];
    if (len + 20 > sizeof(pkt)) return -1;

    struct ip_header* ip = (struct ip_header*)pkt;
    ip->ver_ihl    = 0x45;
    ip->tos        = 0;
    ip->total_len  = sw16(20 + len);
    static uint16_t global_ip_id = 0;
    ip->id         = sw16(global_ip_id++);
    ip->flags_frag = sw16(0x4000);   /* Don't Fragment */
    ip->ttl        = 64;
    ip->protocol   = protocol;
    ip->checksum   = 0;
    ip->src_ip     = sw32(net_ip());
    ip->dst_ip     = sw32(dst_ip);
    ip->checksum   = sw16(ip_checksum(ip, 20));

    const uint8_t* d = (const uint8_t*)data;
    for (uint32_t i = 0; i < len; i++) pkt[20 + i] = d[i];

    return net_send_eth(dst_mac, 0x0800, pkt, 20 + len);
}

void ip_handle(const uint8_t* data, uint32_t len) {
    if (len < 20) return;
    const struct ip_header* ip = (const struct ip_header*)data;

    uint8_t ihl = (ip->ver_ihl & 0x0F) * 4;
    if (ihl < 20 || len < ihl) return;

    uint8_t proto = ip->protocol;
    uint32_t src = sw32(ip->src_ip);

    (void)src;

    if (proto == IP_PROTO_ICMP) {
        icmp_handle(data + ihl, len - ihl);
    }
    else if (proto == IP_PROTO_UDP) {
        uint32_t src = sw32(ip->src_ip);
        udp_handle(src, data + ihl, len - ihl);
    }
    else if (proto == IP_PROTO_TCP) {
        uint32_t src = sw32(ip->src_ip);
        tcp_handle(src, data + ihl, len - ihl);
    }
}
