#include "dns.h"
#include "udp.h"
#include "net.h"
#include "serial.h"
#include <stdint.h>

#define DNS_SERVER 0x0A000203u   /* 10.0.2.3 */
#define DNS_PORT   53
#define LOCAL_PORT 50000

static uint16_t query_id = 0x1234;
static int      waiting = 0;
static uint32_t last_ip = 0;
static char     last_name[64];
static uint16_t last_query_id = 0;

void dns_init(void) {
    waiting = 0;
    last_ip = 0;
}

static int encode_name(const char* name, uint8_t* out, int max) {
    int n = 0;
    const char* p = name;
    while (*p) {
        /* Найти точку или конец */
        const char* dot = p;
        while (*dot && *dot != '.') dot++;
        int seg_len = dot - p;
        if (seg_len == 0 || seg_len > 63) return -1;
        if (n + 1 + seg_len > max) return -1;
        out[n++] = seg_len;
        for (int i = 0; i < seg_len; i++) out[n++] = p[i];
        if (*dot == 0) break;
        p = dot + 1;
    }
    if (n >= max) return -1;
    out[n++] = 0;
    return n;
}

int dns_lookup(const char* name, uint32_t* out_ip) {
    static uint8_t pkt[512];

    /* DNS header */
    pkt[0] = (query_id >> 8) & 0xFF;
    pkt[1] = query_id & 0xFF;
    pkt[2] = 0x01;  /* Standard query, RD=1 */
    pkt[3] = 0x00;
    pkt[4] = 0x00; pkt[5] = 0x01;  /* QDCOUNT = 1 */
    pkt[6] = 0x00; pkt[7] = 0x00;  /* ANCOUNT = 0 */
    pkt[8] = 0x00; pkt[9] = 0x00;  /* NSCOUNT = 0 */
    pkt[10]= 0x00; pkt[11]= 0x00;  /* ARCOUNT = 0 */

    int qlen = encode_name(name, pkt + 12, 200);
    if (qlen < 0) return -1;
    int total = 12 + qlen;

    /* QTYPE=A */
    pkt[total++] = 0x00; pkt[total++] = 0x01;
    /* QCLASS=IN */
    pkt[total++] = 0x00; pkt[total++] = 0x01;

    /* Сохраняем имя и id для ответа */
    int i = 0;
    while (name[i] && i < 63) { last_name[i] = name[i]; i++; }
    last_name[i] = 0;
    last_query_id = query_id;

    if (udp_send(DNS_SERVER, LOCAL_PORT, DNS_PORT, pkt, total) != 0) {
        return -2;
    }

    waiting = 1;
    query_id++;
    (void)out_ip;
    return -1;   /* Ждём ответа */
}

uint32_t dns_last_ip(void) { return last_ip; }
int      dns_pending(void)  { return waiting; }
const char* dns_last_name(void) { return last_name; }

/* Парсинг DNS-ответа */
void dns_handle(const uint8_t* data, uint32_t len) {
    if (len < 12) return;

    uint16_t id    = (data[0] << 8) | data[1];
    uint16_t flags = (data[2] << 8) | data[3];
    uint16_t qd    = (data[4] << 8) | data[5];
    uint16_t an    = (data[6] << 8) | data[7];
    uint8_t  rcode = flags & 0x0F;

    serial_puts("[dns] reply id="); serial_put_hex(id);
    serial_puts(" flags="); serial_put_hex(flags);
    serial_puts(" qd="); serial_put_hex(qd);
    serial_puts(" an="); serial_put_hex(an);
    serial_puts(" rcode="); serial_put_hex(rcode);
    serial_puts("\n");

    if (!(flags & 0x8000)) return;    /* Не ответ */

    if (rcode != 0) {
        serial_puts("[dns] RCODE error\n");
        waiting = 0;
        last_ip = 0;
        return;
    }
    if (an == 0) {
        serial_puts("[dns] no answers\n");
        waiting = 0;
        last_ip = 0;
        return;
    }

    /* Пропускаем QNAME + QTYPE + QCLASS */
    uint32_t off = 12;
    while (off < len && data[off] != 0) {
        off += data[off] + 1;
    }
    off++;
    off += 4;

    for (int i = 0; i < an && off + 10 < len; i++) {
        /* NAME — pointer или labels */
        if ((data[off] & 0xC0) == 0xC0) off += 2;
        else {
            while (off < len && data[off] != 0) off += data[off] + 1;
            off++;
        }
        if (off + 10 > len) break;

        uint16_t type  = (data[off] << 8) | data[off+1];
        uint16_t rdlen = (data[off+8] << 8) | data[off+9];
        off += 10;

        if (type == 1 && rdlen == 4 && off + 4 <= len) {
            last_ip = ((uint32_t)data[off] << 24) |
                      ((uint32_t)data[off+1] << 16) |
                      ((uint32_t)data[off+2] << 8)  |
                      ((uint32_t)data[off+3]);
            waiting = 0;
            serial_puts("[dns] resolved ");
            serial_puts(last_name);
            serial_puts(" -> ");
            serial_put_hex(last_ip);
            serial_puts("\n");
            return;
        }
        off += rdlen;
    }

    /* Не нашли A-запись — но ответ есть */
    waiting = 0;
    last_ip = 0;
}


