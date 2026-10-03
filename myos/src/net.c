#include "net.h"
#include "rtl8139.h"
#include "arp.h"
#include "ip.h"
#include "serial.h"
#include "tty.h"
#include <stdint.h>

static uint8_t our_mac[6];
static uint32_t our_ip = 0;
static int ready = 0;

int net_send_eth(const uint8_t* dst_mac, uint16_t ethertype,
                 const void* data, uint32_t len) {
    if (!ready) return -1;
    static uint8_t frame[2048];
    if (len + 14 > sizeof(frame)) return -1;

    for (int i = 0; i < 6; i++) frame[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) frame[6 + i] = our_mac[i];
    frame[12] = (ethertype >> 8) & 0xFF;
    frame[13] = ethertype & 0xFF;

    const uint8_t* d = (const uint8_t*)data;
    for (uint32_t i = 0; i < len; i++) frame[14 + i] = d[i];

    uint32_t total = 14 + len;

    /* Ethernet minimum frame = 60 байт (без FCS).
       RTL8139 не делает padding автоматически — делаем сами. */
    if (total < 60) {
        for (uint32_t i = total; i < 60; i++) frame[i] = 0;
        total = 60;
    }

    return rtl8139_send(frame, total);
}


void net_init(void) {
    if (rtl8139_init() != 0) return;
    rtl8139_get_mac(our_mac);
    ready = 1;

    /* QEMU user-mode network: 10.0.2.15 */
    our_ip = (10u << 24) | (0u << 16) | (2u << 8) | 15u;

    tty_puts("[net] IP = 10.0.2.15\n");

    /* Сразу ARP для шлюза — чтобы MAC был в кэше */
    extern void ip_prime_gateway(void);
    ip_prime_gateway();
}

void net_poll(void) {
    if (!ready) return;
    static uint8_t buf[2048];
    int n = rtl8139_recv(buf, sizeof(buf));
    if (n > 14) {
        uint16_t ethertype = (buf[12] << 8) | buf[13];
        if (ethertype == 0x0806) {
            arp_handle(buf + 14, n - 14);
        } else if (ethertype == 0x0800) {
            ip_handle(buf + 14, n - 14);
        }
    }
}

int      net_ready(void) { return ready; }
void     net_get_mac(uint8_t* out) { for (int i = 0; i < 6; i++) out[i] = our_mac[i]; }
uint32_t net_ip(void) { return our_ip; }
void     net_set_ip(uint32_t ip) { our_ip = ip; }
