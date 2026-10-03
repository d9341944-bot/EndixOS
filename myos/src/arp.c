#include "arp.h"
#include "net.h"
#include "serial.h"
#include <stdint.h>

#define ARP_CACHE 8
static struct arp_entry cache[ARP_CACHE];

struct arp_packet {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t op;
    uint8_t  sha[6];
    uint32_t spa;
    uint8_t  tha[6];
    uint32_t tpa;
} __attribute__((packed));

static uint16_t sw16(uint16_t v) { return (v >> 8) | (v << 8); }
static uint32_t sw32(uint32_t v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}

void arp_send_request(uint32_t target_ip) {
    static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    struct arp_packet pkt;
    pkt.htype = sw16(1);
    pkt.ptype = sw16(0x0800);
    pkt.hlen  = 6;
    pkt.plen  = 4;
    pkt.op    = sw16(1);

    net_get_mac(pkt.sha);
    pkt.spa = sw32(net_ip());

    for (int i = 0; i < 6; i++) pkt.tha[i] = 0;
    pkt.tpa = sw32(target_ip);

    net_send_eth(bcast, 0x0806, &pkt, sizeof(pkt));

    serial_puts("[arp] request for ");
    serial_put_hex(target_ip);
    serial_puts("\n");
}

static void arp_cache_add(uint32_t ip, const uint8_t* mac) {
    for (int i = 0; i < ARP_CACHE; i++) {
        if (cache[i].valid && cache[i].ip == ip) {
            for (int j = 0; j < 6; j++) cache[i].mac[j] = mac[j];
            return;
        }
    }
    for (int i = 0; i < ARP_CACHE; i++) {
        if (!cache[i].valid) {
            cache[i].valid = 1;
            cache[i].ip = ip;
            for (int j = 0; j < 6; j++) cache[i].mac[j] = mac[j];
            return;
        }
    }
}

void arp_handle(const uint8_t* data, uint32_t len) {
    if (len < sizeof(struct arp_packet)) return;
    const struct arp_packet* p = (const struct arp_packet*)data;

    if (sw16(p->htype) != 1) return;
    if (sw16(p->ptype) != 0x0800) return;

    uint32_t sender_ip = sw32(p->spa);
    uint16_t op = sw16(p->op);

    if (op == 2) {   /* Reply */
        serial_puts("[arp] reply from ");
        serial_put_hex(sender_ip);
        serial_puts(" mac=");
        for (int i = 0; i < 6; i++) {
            serial_put_hex(p->sha[i]);
            if (i < 5) serial_putc(':');
        }
        serial_putc('\n');
        arp_cache_add(sender_ip, p->sha);
    }
}

int arp_lookup(uint32_t ip, uint8_t* out_mac) {
    for (int i = 0; i < ARP_CACHE; i++) {
        if (cache[i].valid && cache[i].ip == ip) {
            for (int j = 0; j < 6; j++) out_mac[j] = cache[i].mac[j];
            return 0;
        }
    }
    return -1;
}
