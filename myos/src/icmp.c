#include "icmp.h"
#include "ip.h"
#include "net.h"
#include "serial.h"
#include <stdint.h>

static uint16_t sw16(uint16_t v) { return (v >> 8) | (v << 8); }

struct icmp_header {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

static uint16_t ping_id = 0x4242;
static uint16_t ping_seq = 0;
static int      last_reply = -1;

void icmp_send_ping(uint32_t dst_ip) {
    static uint8_t pkt[64];
    struct icmp_header* icmp = (struct icmp_header*)pkt;

    icmp->type = 8;      /* Echo Request */
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = sw16(ping_id);
    icmp->seq = sw16(ping_seq++);

    /* Payload — фиксированные 32 байта */
    for (int i = 8; i < 40; i++) pkt[i] = i;

    icmp->checksum = ip_checksum(pkt, 40);

    serial_puts("[icmp] ping to ");
    serial_put_hex(dst_ip);
    serial_puts("\n");

    if (ip_send(dst_ip, IP_PROTO_ICMP, pkt, 40) != 0) {
        serial_puts("[icmp] send failed (need ARP first)\n");
    }
}

void icmp_handle(const uint8_t* data, uint32_t len) {
    if (len < 8) return;
    const struct icmp_header* icmp = (const struct icmp_header*)data;

    if (icmp->type == 0) {   /* Echo Reply */
        uint16_t id  = sw16(icmp->id);
        uint16_t seq = sw16(icmp->seq);

        serial_puts("[icmp] reply id=");
        serial_put_hex(id);
        serial_puts(" seq=");
        serial_put_hex(seq);
        serial_puts("\n");

        /* Считаем время от последнего запроса — но у нас нет точных ms.
           Просто запоминаем номер seq. */
        last_reply = seq;
    }
}

int icmp_last_reply_ms(void) { return last_reply; }
