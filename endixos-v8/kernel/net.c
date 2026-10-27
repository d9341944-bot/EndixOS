/* Сетевой стек EndixOS. Всё в целых числах и статических буферах. */
#include "net.h"

#define PIT_HZ 100
#define ETH_IP  0x0800
#define ETH_ARP 0x0806

static u8 mymac[6];
static net_tx_fn txf;
static net_log_fn logf;
static u32 now;
static int present, changed;

static u32 my_ip, my_mask, my_gw, my_dns, dhcp_srv;
static int ip_up;

static const u8 BCAST[6] = { 255, 255, 255, 255, 255, 255 };

/* ---------- утилиты ---------- */

static void p16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void p32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u32 g16(const u8 *p) { return (u32)p[0] << 8 | p[1]; }
static u32 g32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void mcopy(void *d, const void *s, u32 n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; }
static int meq(const void *d, const void *s, u32 n) { const u8 *a = d, *b = s; while (n--) if (*a++ != *b++) return 0; return 1; }
static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

static u16 csum(const u8 *p, u32 n, u32 init)
{
    u32 s = init;
    for (; n > 1; n -= 2, p += 2) s += (u32)p[0] << 8 | p[1];
    if (n) s += (u32)p[0] << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (u16)~s;
}

u32 net_ip_parse(const char *s, int *ok)
{
    u32 v = 0;
    int parts = 0, digits = 0;
    u32 cur = 0;
    *ok = 0;
    for (;; s++) {
        if (*s >= '0' && *s <= '9') {
            cur = cur * 10 + (u32)(*s - '0');
            if (cur > 255 || ++digits > 3) return 0;
        } else if (*s == '.' || *s == 0) {
            if (!digits) return 0;
            v = v << 8 | cur;
            parts++;
            cur = 0; digits = 0;
            if (*s == 0) break;
            if (parts == 4) return 0;
        } else return 0;
    }
    if (parts != 4) return 0;
    *ok = 1;
    return v;
}

static char *put_u(char *p, u32 v)
{
    char t[11]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = t[--n];
    return p;
}
static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; return p; }

void net_ip_fmt(u32 ip, char *buf)
{
    char *p = buf;
    p = put_u(p, ip >> 24); *p++ = '.';
    p = put_u(p, ip >> 16 & 255); *p++ = '.';
    p = put_u(p, ip >> 8 & 255); *p++ = '.';
    p = put_u(p, ip & 255);
    *p = 0;
}

static void say(const char *s) { if (logf) logf(s); }

/* ---------- Ethernet и ARP ---------- */

static u8 txb[1600];

static int eth_send(const u8 *dst, u32 type, const u8 *payload, u32 len)
{
    if (!txf || len > 1500) return -1;
    mcopy(txb, dst, 6);
    mcopy(txb + 6, mymac, 6);
    p16(txb + 12, type);
    mcopy(txb + 14, payload, len);
    u32 n = 14 + len;
    while (n < 60) txb[n++] = 0;                      /* минимальный кадр */
    return txf(txb, n);
}

typedef struct { u32 ip; u8 mac[6]; u8 used; } arp_t;
static arp_t arp[8];
static int arp_next;

static int dns_kick, tcp_kick;

static void arp_learn(u32 ip, const u8 *mac)
{
    dns_kick = 1; tcp_kick = 1;
    for (int i = 0; i < 8; i++)
        if (arp[i].used && arp[i].ip == ip) { mcopy(arp[i].mac, mac, 6); return; }
    arp_t *a = &arp[arp_next++ & 7];
    a->ip = ip; a->used = 1; mcopy(a->mac, mac, 6);
}

static const u8 *arp_find(u32 ip)
{
    for (int i = 0; i < 8; i++)
        if (arp[i].used && arp[i].ip == ip) return arp[i].mac;
    return 0;
}

static void arp_send(u32 op, const u8 *dstmac, u32 dstip)
{
    u8 b[28];
    p16(b, 1); p16(b + 2, ETH_IP); b[4] = 6; b[5] = 4; p16(b + 6, op);
    mcopy(b + 8, mymac, 6); p32(b + 14, my_ip);
    if (op == 1) { for (int i = 0; i < 6; i++) b[18 + i] = 0; }
    else mcopy(b + 18, dstmac, 6);
    p32(b + 24, dstip);
    eth_send(op == 1 ? BCAST : dstmac, ETH_ARP, b, 28);
}

static void arp_rx(const u8 *p, u32 len)
{
    if (len < 28 || g16(p) != 1 || g16(p + 2) != ETH_IP) return;
    u32 op = g16(p + 6), sip = g32(p + 14), tip = g32(p + 24);
    if (sip) arp_learn(sip, p + 8);
    if (op == 1 && ip_up && tip == my_ip) arp_send(2, p + 8, sip);
}

/* ---------- IPv4 ---------- */

static u16 ip_id;

static int ip_send_raw(u32 src, u32 dst, u32 proto, const u8 *data, u32 len, const u8 *dstmac)
{
    u8 b[1500];
    if (len > 1480) return -1;
    b[0] = 0x45; b[1] = 0;
    p16(b + 2, 20 + len);
    p16(b + 4, ++ip_id);
    p16(b + 6, 0x4000);
    b[8] = 64; b[9] = (u8)proto;
    p16(b + 10, 0);
    p32(b + 12, src); p32(b + 16, dst);
    p16(b + 10, csum(b, 20, 0));
    mcopy(b + 20, data, len);
    return eth_send(dstmac, ETH_IP, b, 20 + len);
}

static int ip_send(u32 dst, u32 proto, const u8 *data, u32 len)
{
    if (!ip_up) return -1;
    u32 hop = ((dst ^ my_ip) & my_mask) == 0 ? dst : my_gw;
    const u8 *mac = arp_find(hop);
    if (!mac) { arp_send(1, BCAST, hop); return -1; }  /* пакет отброшен: верхний уровень повторит */
    return ip_send_raw(my_ip, dst, proto, data, len, mac);
}

static int udp_send(u32 dst, u32 sport, u32 dport, const u8 *data, u32 len)
{
    u8 b[1480];
    if (len > 1400) return -1;
    p16(b, sport); p16(b + 2, dport); p16(b + 4, 8 + len); p16(b + 6, 0);
    mcopy(b + 8, data, len);
    return ip_send(dst, 17, b, 8 + len);
}

/* ---------- DHCP ---------- */

enum { D_IDLE, D_DISCOVER, D_REQUEST, D_BOUND };
static int dstate;
static u32 d_t0, d_xid, d_offer_ip, d_offer_srv;

static void dhcp_send(int type)
{
    u8 b[300];
    for (u32 i = 0; i < sizeof(b); i++) b[i] = 0;
    b[0] = 1; b[1] = 1; b[2] = 6; b[3] = 0;
    p32(b + 4, d_xid);
    p16(b + 10, 0x8000);                               /* ответ широковещательно: у нас ещё нет адреса */
    mcopy(b + 28, mymac, 6);
    p32(b + 236, 0x63825363u);
    u8 *o = b + 240;
    *o++ = 53; *o++ = 1; *o++ = (u8)type;
    *o++ = 61; *o++ = 7; *o++ = 1; mcopy(o, mymac, 6); o += 6;
    if (type == 3) {
        *o++ = 50; *o++ = 4; p32(o, d_offer_ip); o += 4;
        *o++ = 54; *o++ = 4; p32(o, d_offer_srv); o += 4;
    }
    *o++ = 55; *o++ = 3; *o++ = 1; *o++ = 3; *o++ = 6;
    *o++ = 255;
    u8 u[8 + 300];
    u32 n = (u32)(o - b);
    p16(u, 68); p16(u + 2, 67); p16(u + 4, 8 + n); p16(u + 6, 0);
    mcopy(u + 8, b, n);
    ip_send_raw(0, 0xFFFFFFFFu, 17, u, 8 + n, BCAST);
}

static void dhcp_rx(const u8 *p, u32 len)
{
    if (len < 241 || p[0] != 2 || g32(p + 4) != d_xid || !meq(p + 28, mymac, 6) || g32(p + 236) != 0x63825363u) return;
    u32 yi = g32(p + 16), type = 0, mask = 0, gw = 0, dns = 0, srv = 0;
    for (u32 i = 240; i < len;) {
        u32 code = p[i];
        if (code == 255) break;
        if (code == 0) { i++; continue; }
        if (i + 1 >= len) break;
        u32 l = p[i + 1];
        if (i + 2 + l > len) break;
        const u8 *v = p + i + 2;
        if (code == 53 && l >= 1) type = v[0];
        else if (code == 1 && l >= 4) mask = g32(v);
        else if (code == 3 && l >= 4) gw = g32(v);
        else if (code == 6 && l >= 4) dns = g32(v);
        else if (code == 54 && l >= 4) srv = g32(v);
        i += 2 + l;
    }
    if (type == 2 && dstate == D_DISCOVER) {           /* OFFER */
        d_offer_ip = yi; d_offer_srv = srv;
        dstate = D_REQUEST; d_t0 = now;
        dhcp_send(3);
    } else if (type == 5 && dstate == D_REQUEST) {     /* ACK */
        my_ip = yi; my_mask = mask ? mask : 0xFFFFFF00u; my_gw = gw; my_dns = dns ? dns : gw; dhcp_srv = srv;
        ip_up = 1; dstate = D_BOUND; changed = 1;
        for (int i = 0; i < 8; i++) arp[i].used = 0;
    } else if (type == 6 && dstate == D_REQUEST) {     /* NAK: начать заново */
        dstate = D_DISCOVER; d_t0 = now - 2 * PIT_HZ;
    }
}

/* ---------- ICMP и ping ---------- */

static void icmp_rx(u32 src, const u8 *p, u32 len, u32 ttl);

static void icmp_rx_req(u32 src, const u8 *p, u32 len)
{
    if (len < 8 || len > 1400) return;
    u8 b[1480];
    mcopy(b, p, len);
    b[0] = 0; p16(b + 2, 0);
    p16(b + 2, csum(b, len, 0));
    ip_send(src, 1, b, len);
}

/* ---------- DNS ---------- */

#define DNS_PORT 53530
static struct {
    int busy;
    char name[64];
    u16 id;
    u32 t0, tries;
    net_resolve_cb done;
    void *arg;
} dns;

static int dns_query_send(void)
{
    u8 b[300];
    u32 n = 12;
    for (u32 i = 0; i < 12; i++) b[i] = 0;
    p16(b, dns.id); p16(b + 2, 0x0100); p16(b + 4, 1);
    const char *s = dns.name;
    while (*s) {
        u32 l = 0;
        while (s[l] && s[l] != '.') l++;
        if (l == 0 || l > 63 || n + l + 6 > sizeof(b)) return -1;
        b[n++] = (u8)l;
        for (u32 i = 0; i < l; i++) b[n++] = (u8)s[i];
        s += l;
        if (*s == '.') s++;
    }
    b[n++] = 0;
    p16(b + n, 1); p16(b + n + 2, 1); n += 4;
    return udp_send(my_dns, DNS_PORT, 53, b, n);
}

static void dns_finish(u32 ip, int ok)
{
    net_resolve_cb cb = dns.done;
    void *arg = dns.arg;
    dns.busy = 0;
    if (cb) cb(ip, ok, arg);
}

static u32 dns_skip_name(const u8 *p, u32 len, u32 i)
{
    while (i < len) {
        u32 l = p[i];
        if (l == 0) return i + 1;
        if ((l & 0xC0) == 0xC0) return i + 2;
        i += l + 1;
    }
    return len;
}

static void dns_rx(const u8 *p, u32 len)
{
    if (!dns.busy || len < 12 || g16(p) != dns.id || !(p[2] & 0x80)) return;
    if ((p[3] & 15) != 0) { dns_finish(0, 0); return; }
    u32 qd = g16(p + 4), an = g16(p + 6), i = 12;
    for (u32 q = 0; q < qd; q++) { i = dns_skip_name(p, len, i); i += 4; }
    for (u32 a = 0; a < an && i < len; a++) {
        i = dns_skip_name(p, len, i);
        if (i + 10 > len) break;
        u32 type = g16(p + i), rl = g16(p + i + 8);
        i += 10;
        if (i + rl > len) break;
        if (type == 1 && rl == 4) { dns_finish(g32(p + i), 1); return; }
        i += rl;
    }
    dns_finish(0, 0);
}

int net_resolve(const char *host, net_resolve_cb done, void *arg)
{
    if (dns.busy || slen(host) > 62) return -1;
    u32 i = 0;
    while (host[i]) { dns.name[i] = host[i]; i++; }
    dns.name[i] = 0;
    dns.busy = 1; dns.done = done; dns.arg = arg;
    dns.id = (u16)(now * 7919u + 1);
    dns.t0 = now; dns.tries = 1;
    dns_query_send();
    return 0;
}

/* ---------- фоновые команды ---------- */

enum { J_NONE, J_PING_DNS, J_PING, J_LOOKUP, J_HTTP_DNS, J_HTTP };
static struct {
    int kind;
    char host[64];
    u32 ip, count, sent, got, t_sent, next_send, seq_open;
    u32 min_t, max_t, sum_t;
    u8 outstanding;
} job;

static void job_summary(void)
{
    char b[96], *p = b;
    say("");
    p = put_s(p, "--- "); p = put_s(p, job.host); p = put_s(p, " ping statistics ---");
    *p = 0; say(b);
    p = b;
    p = put_u(p, job.sent); p = put_s(p, " packets transmitted, ");
    p = put_u(p, job.got); p = put_s(p, " packets received, ");
    p = put_u(p, job.sent ? (job.sent - job.got) * 100 / job.sent : 0); p = put_s(p, "% packet loss");
    *p = 0; say(b);
    if (job.got) {
        p = b;
        p = put_s(p, "round-trip min/avg/max = ");
        p = put_u(p, job.min_t * 10); *p++ = '/'; p = put_u(p, job.sum_t * 10 / job.got); *p++ = '/'; p = put_u(p, job.max_t * 10);
        p = put_s(p, " ms");
        *p = 0; say(b);
    }
    job.kind = J_NONE;
}

static void ping_start(u32 ip)
{
    char b[120], a[16], *p = b;
    job.ip = ip; job.kind = J_PING;
    job.sent = job.got = 0; job.next_send = now; job.outstanding = 0; job.sum_t = job.max_t = 0; job.min_t = 0xFFFFFF;
    net_ip_fmt(ip, a);
    p = put_s(p, "PING "); p = put_s(p, job.host); p = put_s(p, " ("); p = put_s(p, a); p = put_s(p, "): 56 data bytes");
    *p = 0; say(b);
}

static void ping_dns_done(u32 ip, int ok, void *arg)
{
    (void)arg;
    if (job.kind != J_PING_DNS) return;
    if (!ok) {
        char b[96], *p = b;
        p = put_s(p, "ping: cannot resolve "); p = put_s(p, job.host); p = put_s(p, ": Unknown host");
        *p = 0; say(b);
        job.kind = J_NONE;
        return;
    }
    ping_start(ip);
}

static void lookup_done(u32 ip, int ok, void *arg)
{
    (void)arg;
    if (job.kind != J_LOOKUP) return;
    char b[96], a[16], *p = b;
    if (!ok) {
        p = put_s(p, "** server can't find "); p = put_s(p, job.host); p = put_s(p, ": NXDOMAIN");
        *p = 0; say(b);
    } else {
        net_ip_fmt(my_dns, a);
        p = put_s(p, "Server:  "); p = put_s(p, a); *p = 0; say(b);
        net_ip_fmt(ip, a);
        p = b; p = put_s(p, "Name:    "); p = put_s(p, job.host); *p = 0; say(b);
        p = b; p = put_s(p, "Address: "); p = put_s(p, a); *p = 0; say(b);
    }
    job.kind = J_NONE;
}

static int job_begin(const char *host)
{
    if (job.kind != J_NONE || dns.busy) { say("network: busy"); return -1; }
    if (!ip_up) { say("network is unreachable (no IP address yet)"); return -1; }
    u32 i = 0;
    while (host[i] && i < 63) { job.host[i] = host[i]; i++; }
    job.host[i] = 0;
    return 0;
}

int net_ping(const char *host, int count)
{
    if (job_begin(host) < 0) return -1;
    job.count = (u32)(count > 0 ? count : 4);
    int ok;
    u32 ip = net_ip_parse(host, &ok);
    if (ok) { ping_start(ip); return 0; }
    job.kind = J_PING_DNS;
    if (net_resolve(host, ping_dns_done, 0) < 0) { job.kind = J_NONE; return -1; }
    return 0;
}

int net_nslookup(const char *host)
{
    if (job_begin(host) < 0) return -1;
    int ok;
    u32 ip = net_ip_parse(host, &ok);
    job.kind = J_LOOKUP;
    if (ok) { lookup_done(ip, 1, 0); return 0; }
    if (net_resolve(host, lookup_done, 0) < 0) { job.kind = J_NONE; return -1; }
    return 0;
}

int net_job_active(void) { return job.kind != J_NONE; }

static void http_stop(void);

void net_stop(void)
{
    if (job.kind == J_PING) job_summary();
    else if (job.kind != J_NONE) { dns.busy = 0; http_stop(); job.kind = J_NONE; }
}


/* ---------- TCP (один исходящий клиент) и HTTP ---------- */

enum { T_CLOSED, T_SYN, T_EST };
static struct {
    int state;
    u32 ip, lport, rport;
    u32 iss, snd_una, snd_nxt, rcv_nxt;
    u8 last[1200]; u32 last_len, last_seq; u8 last_flags;   /* неподтверждённый сегмент */
    u32 t0, tries;
} tcp;

static char http_path[128], http_hostname[64], http_loc[96];
static int http_in_hdr, http_status, http_show_hdr;
static u32 http_body, http_port;
static char http_line[110]; static u32 http_ll;
static u32 http_tail;                                 /* последние 4 байта заголовка */

static void http_flush(void) { if (http_ll) { http_line[http_ll] = 0; say(http_line); http_ll = 0; } }

static void http_put(char c)
{
    if (c == '\n') { http_flush(); return; }
    if (http_ll >= sizeof(http_line) - 2) http_flush();
    http_line[http_ll++] = c;
}

static void tcp_seg(u8 flags, u32 seq, const u8 *data, u32 len)
{
    u8 b[1480];
    u32 hl = (flags & 2) ? 24 : 20;
    if (len > 1200) return;
    p16(b, tcp.lport); p16(b + 2, tcp.rport); p32(b + 4, seq); p32(b + 8, (flags & 0x10) ? tcp.rcv_nxt : 0);
    b[12] = (u8)(hl / 4 << 4); b[13] = flags; p16(b + 14, 4096); p16(b + 16, 0); p16(b + 18, 0);
    if (flags & 2) { b[20] = 2; b[21] = 4; p16(b + 22, 1400); }
    mcopy(b + hl, data, len);
    u32 n = hl + len;
    u32 ps = (my_ip >> 16) + (my_ip & 0xFFFF) + (tcp.ip >> 16) + (tcp.ip & 0xFFFF) + 6 + n;
    p16(b + 16, csum(b, n, ps));
    ip_send(tcp.ip, 6, b, n);
}

static void tcp_send_data(u8 flags, const u8 *data, u32 len)
{
    tcp.last_seq = tcp.snd_nxt; tcp.last_flags = flags; tcp.last_len = len;
    mcopy(tcp.last, data, len);
    tcp.t0 = now; tcp.tries = 1;
    tcp_seg(flags, tcp.snd_nxt, data, len);
    tcp.snd_nxt += len + ((flags & 3) ? 1 : 0);
}

static void tcp_abort(void)
{
    if (tcp.state == T_EST) tcp_seg(0x14, tcp.snd_nxt, 0, 0);   /* RST|ACK */
    tcp.state = T_CLOSED;
}

static void http_stop(void) { http_flush(); tcp_abort(); }

static void http_end(const char *err)
{
    http_flush();
    if (err) say(err);
    else if (http_in_hdr) say("curl: empty reply from server");
    tcp.state = T_CLOSED;
    job.kind = J_NONE;
}

static void http_hdr_line(void)
{
    http_line[http_ll] = 0;
    const char *l = http_line;
    if (!http_status && l[0] == 'H') {                 /* "HTTP/1.x CODE ..." */
        u32 i = 0; while (l[i] && l[i] != ' ') i++;
        while (l[i] == ' ') i++;
        while (l[i] >= '0' && l[i] <= '9') http_status = http_status * 10 + (l[i++] - '0');
        if (!http_status) http_status = 1;
        if (!http_show_hdr) say(l);
    } else if ((l[0] | 32) == 'l' && (l[1] | 32) == 'o' && (l[2] | 32) == 'c' && (l[3] | 32) == 'a' && l[8] == ':') {
        const char *v = l + 9; while (*v == ' ') v++;
        u32 i = 0; while (v[i] && i < sizeof(http_loc) - 1) { http_loc[i] = v[i]; i++; }
        http_loc[i] = 0;
    }
    if (http_show_hdr) say(l);
    http_ll = 0;
}

static void http_data(const u8 *d, u32 n)
{
    for (u32 i = 0; i < n; i++) {
        u8 c = d[i];
        if (http_in_hdr) {
            http_tail = http_tail << 8 | c;
            if (c == '\n') { http_hdr_line(); }
            else if (c != '\r' && http_ll < sizeof(http_line) - 2) http_line[http_ll++] = (char)c;
            if ((http_tail & 0xFFFF) == 0x0A0A || http_tail == 0x0D0A0D0Au) {   /* пустая строка */
                http_in_hdr = 0;
                if (http_show_hdr) say("");
                if (http_status >= 300 && http_status < 400 && http_loc[0]) {
                    char b[140], *q = b;
                    q = put_s(q, "-> redirect: "); q = put_s(q, http_loc); *q = 0; say(b);
                }
            }
            continue;
        }
        if (http_body >= 8192) {
            http_flush(); say("[output truncated at 8 KB]");
            tcp_abort(); http_end(0); return;
        }
        http_body++;
        if (c == '\r') continue;
        if (c == '\t') c = ' ';
        if (c >= 0x80) { if (c >= 0xC0) http_put('?'); continue; }
        if (c < 32 && c != '\n') continue;
        http_put((char)c);
    }
}

static void http_request(void)
{
    u8 b[400]; char *p = (char *)b;
    p = put_s(p, "GET "); p = put_s(p, http_path); p = put_s(p, " HTTP/1.0\r\nHost: "); p = put_s(p, http_hostname);
    p = put_s(p, "\r\nUser-Agent: EndixOS/8.0\r\nAccept: */*\r\nConnection: close\r\n\r\n");
    tcp_send_data(0x18, b, (u32)((u8 *)p - b));
}

static void tcp_rx(u32 src, const u8 *p, u32 len)
{
    if (len < 20 || tcp.state == T_CLOSED || src != tcp.ip || g16(p + 2) != tcp.lport || g16(p) != tcp.rport) return;
    u32 hl = (u32)(p[12] >> 4) * 4, fl = p[13], seq = g32(p + 4), ack = g32(p + 8);
    if (hl < 20 || hl > len) return;
    const u8 *d = p + hl; u32 dl = len - hl;
    if (fl & 4) { if (job.kind == J_HTTP) http_end("curl: connection refused or reset"); else tcp.state = T_CLOSED; return; }
    if (tcp.state == T_SYN) {
        if ((fl & 0x12) == 0x12 && ack == tcp.iss + 1) {
            tcp.rcv_nxt = seq + 1; tcp.snd_una = ack; tcp.state = T_EST; tcp.last_len = 0; tcp.last_flags = 0;
            tcp_seg(0x10, tcp.snd_nxt, 0, 0);
            http_request();
        }
        return;
    }
    if ((fl & 0x10) && (i32)(ack - tcp.snd_una) > 0 && (i32)(ack - tcp.snd_nxt) <= 0) {
        tcp.snd_una = ack;
        if (ack == tcp.snd_nxt) tcp.last_flags = 0, tcp.last_len = 0, tcp.tries = 0;
    }
    if (dl) {
        if (seq == tcp.rcv_nxt) {
            tcp.rcv_nxt += dl;
            tcp_seg(0x10, tcp.snd_nxt, 0, 0);
            if (job.kind == J_HTTP) http_data(d, dl);
            if (tcp.state == T_CLOSED) return;        /* оборвано в http_data */
        } else { tcp_seg(0x10, tcp.snd_nxt, 0, 0); return; }
    }
    if ((fl & 1) && seq + dl == tcp.rcv_nxt) {          /* FIN: подтверждаем и закрываемся сами */
        tcp.rcv_nxt++;
        tcp_seg(0x11, tcp.snd_nxt, 0, 0);
        tcp.state = T_CLOSED;
        if (job.kind == J_HTTP) http_end(0);
    }
}

static void tcp_tick(void)
{
    if (tcp.state == T_CLOSED || job.kind != J_HTTP) return;
    int unacked = tcp.state == T_SYN || tcp.last_flags;
    if (!unacked) return;
    int kick = tcp_kick && tcp.tries == 1 && now != tcp.t0;
    if (kick || (i32)(now - tcp.t0) >= PIT_HZ) {
        if (!kick && tcp.tries >= 5) { tcp.state = T_CLOSED; http_end("curl: connection timed out"); return; }
        if (!kick) tcp.tries++;
        tcp.t0 = now;
        tcp_seg(tcp.last_flags, tcp.last_seq, tcp.last, tcp.last_len);
    }
}

static void http_dns_done(u32 ip, int ok, void *arg)
{
    (void)arg;
    if (job.kind != J_HTTP_DNS) return;
    if (!ok) {
        char b[110], *q = b;
        q = put_s(q, "curl: could not resolve host: "); q = put_s(q, http_hostname); *q = 0; say(b);
        job.kind = J_NONE; return;
    }
    job.kind = J_HTTP;
    tcp.ip = ip; tcp.rport = http_port; tcp.lport = 40000 + (now & 0x3FF);
    tcp.iss = now * 2654435u + 12345u; tcp.snd_nxt = tcp.iss; tcp.snd_una = tcp.iss;
    tcp.state = T_SYN; tcp.rcv_nxt = 0;
    http_in_hdr = 1; http_status = 0; http_body = 0; http_tail = 0; http_ll = 0; http_loc[0] = 0;
    tcp_send_data(0x02, 0, 0);
}

int net_http_get(const char *url, int show_headers)
{
    const char *u = url;
    if (u[0] == 'h' && u[1] == 't' && u[2] == 't' && u[3] == 'p') {
        if (u[4] == 's' && u[5] == ':') { say("curl: https:// is not supported yet (TLS is not implemented). Use http://"); return -1; }
        if (u[4] == ':' && u[5] == '/' && u[6] == '/') u += 7;
    }
    if (job.kind != J_NONE || dns.busy) { say("network: busy"); return -1; }
    if (!ip_up) { say("network is unreachable (no IP address yet)"); return -1; }
    u32 i = 0;
    while (*u && *u != '/' && *u != ':' && i < sizeof(http_hostname) - 1) http_hostname[i++] = *u++;
    http_hostname[i] = 0;
    http_port = 80;
    if (*u == ':') { u++; http_port = 0; while (*u >= '0' && *u <= '9') http_port = http_port * 10 + (u32)(*u++ - '0'); }
    if (!i || http_port == 0 || http_port > 65535) { say("curl: bad URL"); return -1; }
    i = 0;
    if (*u != '/') http_path[i++] = '/';
    while (*u && i < sizeof(http_path) - 1) http_path[i++] = *u++;
    http_path[i] = 0;
    http_show_hdr = show_headers;
    for (i = 0; http_hostname[i]; i++) job.host[i] = http_hostname[i];
    job.host[i] = 0;
    int ok;
    u32 ip = net_ip_parse(http_hostname, &ok);
    job.kind = J_HTTP_DNS;
    if (ok) { http_dns_done(ip, 1, 0); return 0; }
    if (net_resolve(http_hostname, http_dns_done, 0) < 0) { job.kind = J_NONE; return -1; }
    return 0;
}

static void icmp_rx(u32 src, const u8 *p, u32 len, u32 ttl)
{
    if (len < 8) return;
    if (p[0] == 8) { icmp_rx_req(src, p, len); return; }
    if (p[0] != 0 || job.kind != J_PING || src != job.ip || g16(p + 4) != 0xE1D1) return;
    u32 seq = g16(p + 6);
    if (seq + 1 != job.sent || !job.outstanding) return;
    job.outstanding = 0;
    job.got++;
    u32 dt = now - job.t_sent;
    if (dt < job.min_t) job.min_t = dt;
    if (dt > job.max_t) job.max_t = dt;
    job.sum_t += dt;
    char b[120], a[16], *q = b;
    net_ip_fmt(src, a);
    q = put_u(q, len); q = put_s(q, " bytes from "); q = put_s(q, a);
    q = put_s(q, ": icmp_seq="); q = put_u(q, seq); q = put_s(q, " ttl="); q = put_u(q, ttl);
    if (dt == 0) q = put_s(q, " time=<10 ms");
    else { q = put_s(q, " time="); q = put_u(q, dt * 10); q = put_s(q, " ms"); }
    *q = 0;
    say(b);
}

/* ---------- приём ---------- */

static void ip_rx(const u8 *p, u32 len)
{
    if (len < 20 || (p[0] >> 4) != 4) return;
    u32 ihl = (u32)(p[0] & 15) * 4, tl = g16(p + 2);
    if (ihl < 20 || tl < ihl || tl > len) return;
    if (g16(p + 6) & 0x3FFF) return;                   /* фрагменты не собираем */
    u32 dst = g32(p + 16), src = g32(p + 12);
    if (ip_up && dst != my_ip && dst != 0xFFFFFFFFu) return;
    const u8 *d = p + ihl;
    u32 dl = tl - ihl;
    if (p[9] == 1) icmp_rx(src, d, dl, p[8]);
    else if (p[9] == 17 && dl >= 8) {
        u32 sp = g16(d), dp = g16(d + 2);
        (void)sp;
        if (dp == 68) dhcp_rx(d + 8, dl - 8);
        else if (dp == DNS_PORT) dns_rx(d + 8, dl - 8);
    } else if (p[9] == 6) tcp_rx(src, d, dl);
}

void net_rx(const u8 *f, u32 len)
{
    if (len < 14) return;
    if (!meq(f, mymac, 6) && !meq(f, BCAST, 6)) return;
    u32 type = g16(f + 12);
    if (type == ETH_ARP) arp_rx(f + 14, len - 14);
    else if (type == ETH_IP) ip_rx(f + 14, len - 14);
}

/* ---------- таймеры ---------- */

void net_tick(u32 t)
{
    now = t;
    if (!present) return;
    if (dstate == D_IDLE || ((dstate == D_DISCOVER || dstate == D_REQUEST) && (i32)(now - d_t0) >= 3 * PIT_HZ)) {
        dstate = D_DISCOVER; d_t0 = now;
        dhcp_send(1);
    }
    if (dns.busy && dns_kick && dns.tries == 1 && now != dns.t0) { dns_kick = 0; dns.t0 = now; dns_query_send(); }
    dns_kick = 0;
    if (dns.busy && (i32)(now - dns.t0) >= PIT_HZ) {
        if (dns.tries >= 4) { dns_finish(0, 0); }
        else { dns.tries++; dns.t0 = now; dns_query_send(); }
    }
    tcp_tick();
    tcp_kick = 0;
    if (job.kind == J_PING) {
        if ((i32)(now - job.next_send) >= 0 && job.sent < job.count) {
            if (job.outstanding) {                      /* прошлый ответ не пришёл */
                char b[64], *p = b;
                p = put_s(p, "Request timeout for icmp_seq "); p = put_u(p, job.sent - 1);
                *p = 0; say(b);
            }
            u8 b[8 + 56];
            b[0] = 8; b[1] = 0; p16(b + 2, 0); p16(b + 4, 0xE1D1); p16(b + 6, job.sent);
            for (int i = 0; i < 56; i++) b[8 + i] = (u8)(0x10 + i);
            p16(b + 2, csum(b, sizeof(b), 0));
            ip_send(job.ip, 1, b, sizeof(b));
            job.sent++; job.outstanding = 1; job.t_sent = now; job.next_send = now + PIT_HZ;
        } else if (job.sent >= job.count && (i32)(now - job.next_send) >= 0) {
            if (job.outstanding) {
                char b[64], *p = b;
                p = put_s(p, "Request timeout for icmp_seq "); p = put_u(p, job.sent - 1);
                *p = 0; say(b);
            }
            job_summary();
        }
    }
}

/* ---------- состояние ---------- */

void net_init(const u8 mac[6], net_tx_fn tx)
{
    mcopy(mymac, mac, 6);
    txf = tx;
    present = 1;
    ip_up = 0; dstate = D_IDLE; job.kind = J_NONE; dns.busy = 0;
    d_xid = 0xE1D10000u | (u32)mac[4] << 8 | mac[5];
    changed = 1;
}

void net_set_log(net_log_fn f) { logf = f; }

void net_info(net_info_t *i)
{
    mcopy(i->mac, mymac, 6);
    i->ip = my_ip; i->mask = my_mask; i->gw = my_gw; i->dns = my_dns; i->up = ip_up; i->present = present;
}

int net_changed(void) { int c = changed; changed = 0; return c; }
