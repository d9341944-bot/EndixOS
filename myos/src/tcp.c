#include "tcp.h"
#include "ip.h"
#include "net.h"
#include "serial.h"
#include <stdint.h>

#define ST_CLOSED      0
#define ST_SYN_SENT    1
#define ST_ESTABLISHED 2
#define ST_FIN_SENT    3

static int      state = ST_CLOSED;
static uint32_t remote_ip = 0;
static uint16_t remote_port = 0;
static uint16_t local_port = 40000;
static uint32_t my_seq = 0;
static uint32_t my_ack = 0;
static uint32_t expected_seq = 0;

static uint8_t  rx_buf[16384];
static uint32_t rx_len = 0;
static int      fin_received = 0;

static uint16_t sw16(uint16_t v) { return (v >> 8) | (v << 8); }
static uint32_t sw32(uint32_t v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}

static uint32_t rng = 0xDEADBEEF;
static uint32_t rnd(void) { rng = rng * 1103515245 + 12345; return rng; }

/* TCP checksum с псевдо-заголовком */
static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                              const uint8_t* tcp_seg, uint32_t tcp_len) {
    static uint8_t buf[2048 + 12];
    uint32_t n = 0;

    buf[n++] = (src_ip >> 24) & 0xFF;
    buf[n++] = (src_ip >> 16) & 0xFF;
    buf[n++] = (src_ip >> 8)  & 0xFF;
    buf[n++] = src_ip & 0xFF;
    buf[n++] = (dst_ip >> 24) & 0xFF;
    buf[n++] = (dst_ip >> 16) & 0xFF;
    buf[n++] = (dst_ip >> 8)  & 0xFF;
    buf[n++] = dst_ip & 0xFF;
    buf[n++] = 0;
    buf[n++] = 6;
    buf[n++] = (tcp_len >> 8) & 0xFF;
    buf[n++] = tcp_len & 0xFF;
    for (uint32_t i = 0; i < tcp_len; i++) buf[n++] = tcp_seg[i];

    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < n; i += 2)
        sum += ((uint16_t)buf[i] << 8) | buf[i+1];
    if (n & 1) sum += ((uint16_t)buf[n-1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum & 0xFFFF;
}

static void tcp_send_raw(uint8_t flags, const void* data, uint32_t len) {
    static uint8_t pkt[2048];
    struct tcp_header* tcp = (struct tcp_header*)pkt;

    uint32_t seq_now = my_seq;

    /* ИНКРЕМЕНТ: для данных на len, для SYN/FIN на 1.
       Делаем ДО ip_send, чтобы синхронный SYN-ACK не сбил. */
    if (flags & (TCP_SYN | TCP_FIN)) my_seq += 1;
    my_seq += len;

    tcp->src_port = sw16(local_port);
    tcp->dst_port = sw16(remote_port);
    tcp->seq      = sw32(seq_now);
    tcp->ack      = (flags & TCP_ACK) ? sw32(my_ack) : 0;
    tcp->data_off = (5 << 4);
    tcp->flags    = flags;
    tcp->window   = sw16(65535);
    tcp->checksum = 0;
    tcp->urgent   = 0;

    if (len > 0 && data) {
        const uint8_t* d = (const uint8_t*)data;
        for (uint32_t i = 0; i < len; i++) pkt[20 + i] = d[i];
    }

    uint32_t total = 20 + len;

    extern uint32_t net_ip(void);
    uint32_t src_ip = net_ip();
    uint16_t csum = tcp_checksum(src_ip, remote_ip, pkt, total);
    tcp->checksum = sw16(csum);

    serial_puts("[tcp] SEND seq=");
    serial_put_hex(seq_now);
    serial_puts(" ack=");
    serial_put_hex(my_ack);
    serial_puts(" fl=");
    serial_put_hex(flags);
    serial_puts(" next=");
    serial_put_hex(my_seq);
    serial_putc('\n');

    ip_send(remote_ip, 6, pkt, total);
}

int tcp_connect(uint32_t ip, uint16_t port) {
    serial_puts("[tcp] CONNECT state=");
    serial_put_hex(state);
    serial_putc('\n');

    if (state == ST_SYN_SENT || state == ST_ESTABLISHED) {
        serial_puts("[tcp] ALREADY, ignore\n");
        return 0;
    }

    remote_ip = ip;
    remote_port = port;
    local_port = 40000 + (rnd() % 10000);
    my_seq = rnd();
    my_ack = 0;
    rx_len = 0;
    fin_received = 0;

    state = ST_SYN_SENT;
    tcp_send_raw(TCP_SYN, 0, 0);

    serial_puts("[tcp] SYN to ");
    serial_put_hex(ip);
    serial_puts(":");
    serial_put_hex(port);
    serial_putc('\n');
    return 0;
}

int tcp_send(const void* data, uint32_t len) {
    if (state != ST_ESTABLISHED) return -1;
    tcp_send_raw(TCP_PSH | TCP_ACK, data, len);
    return len;
}

void tcp_close(void) {
    if (state == ST_ESTABLISHED) {
        tcp_send_raw(TCP_FIN | TCP_ACK, 0, 0);
        state = ST_FIN_SENT;
    }
}

void tcp_handle(uint32_t src_ip, const uint8_t* data, uint32_t len) {
    (void)src_ip;
    if (len < 20) return;
    const struct tcp_header* tcp = (const struct tcp_header*)data;

    uint16_t sport = sw16(tcp->src_port);
    uint16_t dport = sw16(tcp->dst_port);
    if (sport != remote_port || dport != local_port) return;

    uint32_t seq = sw32(tcp->seq);
    uint8_t flags = tcp->flags;
    uint8_t off = (tcp->data_off >> 4) * 4;
    if (off < 20 || len < off) return;

    uint32_t data_len = len - off;
    const uint8_t* payload = data + off;

    if (state == ST_SYN_SENT) {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            my_ack = seq + 1;
            expected_seq = seq + 1;
            state = ST_ESTABLISHED;
            tcp_send_raw(TCP_ACK, 0, 0);
            serial_puts("[tcp] ESTABLISHED\n");
        }
        return;
    }

    if (state == ST_ESTABLISHED || state == ST_FIN_SENT) {
        if (data_len > 0) {
            if (seq == expected_seq) {
                serial_puts("[tcp] DATA ");
                serial_put_hex(data_len);
                serial_puts(" tot=");
                serial_put_hex(rx_len + data_len);
                serial_putc('\n');

                for (uint32_t i = 0; i < data_len && rx_len < sizeof(rx_buf); i++)
                    rx_buf[rx_len++] = payload[i];
                expected_seq = seq + data_len;
            } else {
                serial_puts("[tcp] RETX seq=");
                serial_put_hex(seq);
                serial_puts(" exp=");
                serial_put_hex(expected_seq);
                serial_putc('\n');
            }
            my_ack = expected_seq;
            tcp_send_raw(TCP_ACK, 0, 0);
        }
        if (flags & TCP_FIN) {
            my_ack = seq + data_len + 1;
            tcp_send_raw(TCP_ACK, 0, 0);
            fin_received = 1;
            state = ST_CLOSED;
            serial_puts("[tcp] FIN received\n");
        }
    }
}

int tcp_state(void) { return state; }

int tcp_recv(void* buf, uint32_t max) {
    if (rx_len > 0) {
        uint32_t n = rx_len < max ? rx_len : max;
        uint8_t* d = (uint8_t*)buf;
        for (uint32_t i = 0; i < n; i++) d[i] = rx_buf[i];
        for (uint32_t i = n; i < rx_len; i++) rx_buf[i - n] = rx_buf[i];
        rx_len -= n;
        return n;
    }
    if (fin_received) return 0;
    return -1;
}

void tcp_init(void) {
    state = ST_CLOSED;
    rx_len = 0;
}
