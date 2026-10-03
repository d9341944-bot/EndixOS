#include "terminal.h"
#include "wm.h"
#include "framebuffer.h"
#include "settings.h"
#include "fat16.h"
#include "keyboard.h"
#include <stdint.h>

int g_terminal_wants_exit = 0;

extern volatile uint32_t ticks;

#define TERM_LINES     26
#define TERM_LINE_LEN  72

static char lines[TERM_LINES][TERM_LINE_LEN];
static int  n_lines = 0;
static char input[TERM_LINE_LEN];
static int  input_len = 0;
static int  win_id = -1;

/* --------- утилиты --------- */

static void term_push(const char* s) {
    if (n_lines >= TERM_LINES) {
        for (int i = 1; i < TERM_LINES; i++)
            for (int j = 0; j < TERM_LINE_LEN; j++)
                lines[i-1][j] = lines[i][j];
        n_lines = TERM_LINES - 1;
    }
    int i = 0;
    while (s[i] && i < TERM_LINE_LEN - 1) {
        lines[n_lines][i] = s[i];
        i++;
    }
    lines[n_lines][i] = 0;
    n_lines++;
}

static int st_eq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static int st_pfx(const char* s, const char* p) {
    while (*p) if (*s++ != *p++) return 0;
    return 1;
}
static int st_len(const char* s) { int n = 0; while (s[n]) n++; return n; }

/* --------- ls через callback --------- */

static int ls_cb(const struct fat16_dirent* de, void* user) {
    (void)user;
    char name[16];
    fat16_name(de, name);

    char line[TERM_LINE_LEN];
    int i = 0;
    line[i++] = ' ';
    line[i++] = ' ';
    int k = 0;
    while (name[k] && i < TERM_LINE_LEN - 15) { line[i++] = name[k++]; }
    while (i < 30) line[i++] = ' ';
    line[i++] = '0'; line[i++] = 'x';
    uint32_t s = de->size;
    for (int j = 0; j < 8; j++) {
        uint32_t nib = (s >> ((7-j)*4)) & 0xF;
        line[i++] = nib < 10 ? ('0'+nib) : ('a'+nib-10);
    }
    line[i] = 0;
    term_push(line);
    return 0;
}

/* --------- выполнение команды --------- */

static void term_print_help(void) {
    term_push("");
    term_push("  Commands:");
    term_push("    help         - this help");
    term_push("    ls           - list FAT16 root");
    term_push("    cat <file>   - print file");
    term_push("    echo <text>  - print text");
    term_push("    uname        - system info");
    term_push("    date         - uptime");
    term_push("    clear        - clear screen");
    term_push("    settings     - open Settings window");
    term_push("    exit         - close Terminal");
    term_push("");
}

static void term_print_uname(void) {
    term_push("EndixOS v5.0  i686  /kernel: GDT IDT PIC PIT PMM PG WM ELF FAT16");
}

static void term_print_date(void) {
    uint32_t secs = ticks / 100;
    char buf[32];
    int n = 0;
    buf[n++] = 'u'; buf[n++] = 'p'; buf[n++] = ' ';
    if (secs == 0) buf[n++] = '0';
    else {
        char t[12]; int m = 0;
        while (secs > 0) { t[m++] = '0' + (secs % 10); secs /= 10; }
        while (m > 0) buf[n++] = t[--m];
    }
    buf[n++] = 's'; buf[n] = 0;
    term_push(buf);
}

static void term_cat(const char* name) {
    struct fat16_dirent de;
    if (fat16_find(name, &de) != 0) {
        term_push("  not found");
        return;
    }
    if (de.attr & FAT16_ATTR_DIR) {
        term_push("  it's a directory");
        return;
    }
    static uint8_t buf[2048];
    uint32_t n = fat16_read(&de, buf, sizeof(buf) - 1);
    buf[n] = 0;

    char line[TERM_LINE_LEN];
    int li = 0;
    for (uint32_t j = 0; j < n; j++) {
        if (buf[j] == '\n' || li >= TERM_LINE_LEN - 1) {
            line[li] = 0;
            term_push(line);
            li = 0;
        } else if (buf[j] != '\r') {
            line[li++] = buf[j];
        }
    }
    if (li > 0) { line[li] = 0; term_push(line); }
}

static void cmd_term_net(void) {
    extern int net_ready(void);
    extern uint32_t net_ip(void);
    extern void net_get_mac(uint8_t*);
    if (!net_ready()) { term_push("network not ready"); return; }
    uint8_t mac[6];
    net_get_mac(mac);
    term_push("Network:");
    char buf[48]; int n = 0;
    const char* p = "  MAC: ";
    while (*p) buf[n++] = *p++;
    for (int i = 0; i < 6; i++) {
        uint8_t hi = (mac[i] >> 4) & 0xF;
        uint8_t lo = mac[i] & 0xF;
        buf[n++] = hi < 10 ? '0'+hi : 'a'+hi-10;
        buf[n++] = lo < 10 ? '0'+lo : 'a'+lo-10;
        if (i < 5) buf[n++] = ':';
    }
    buf[n] = 0;
    term_push(buf);

    n = 0;
    p = "  IP:  ";
    while (*p) buf[n++] = *p++;
    uint32_t ip = net_ip();
    uint8_t a = (ip >> 24) & 0xFF;
    uint8_t b = (ip >> 16) & 0xFF;
    uint8_t c = (ip >> 8)  & 0xFF;
    uint8_t d = ip & 0xFF;
    char tmp[16]; int m = 0;
    if (a == 0) tmp[m++] = '0'; else { char t2[4]; int k=0; while(a){t2[k++]='0'+(a%10);a/=10;} while(k)tmp[m++]=t2[--k]; }
    tmp[m++] = '.';
    if (b == 0) tmp[m++] = '0'; else { char t2[4]; int k=0; while(b){t2[k++]='0'+(b%10);b/=10;} while(k)tmp[m++]=t2[--k]; }
    tmp[m++] = '.';
    if (c == 0) tmp[m++] = '0'; else { char t2[4]; int k=0; while(c){t2[k++]='0'+(c%10);c/=10;} while(k)tmp[m++]=t2[--k]; }
    tmp[m++] = '.';
    if (d == 0) tmp[m++] = '0'; else { char t2[4]; int k=0; while(d){t2[k++]='0'+(d%10);d/=10;} while(k)tmp[m++]=t2[--k]; }
    tmp[m] = 0;
    for (int i = 0; i < m; i++) buf[n++] = tmp[i];
    buf[n] = 0;
    term_push(buf);
    term_push("  Gateway: 10.0.2.2");
    term_push("  DNS:     10.0.2.3");
}

static void cmd_term_arp(const char* arg) {
    uint32_t ip = 0;
    uint32_t cur = 0;
    while (*arg) {
        if (*arg >= '0' && *arg <= '9') cur = cur * 10 + (*arg - '0');
        else if (*arg == '.') { ip = (ip << 8) | (cur & 0xFF); cur = 0; }
        arg++;
    }
    ip = (ip << 8) | (cur & 0xFF);
    extern void arp_send_request(uint32_t);
    arp_send_request(ip);
    term_push("ARP request sent (check serial)");
}

static void cmd_term_ping(const char* arg) {
    uint32_t ip = 0;
    uint32_t cur = 0;
    int parts = 0;
    while (*arg && *arg != ' ') {
        if (*arg >= '0' && *arg <= '9') cur = cur * 10 + (*arg - '0');
        else if (*arg == '.') { ip = (ip << 8) | (cur & 0xFF); cur = 0; parts++; }
        arg++;
    }
    if (parts != 3) {
        term_push("usage: ping 10.0.2.2");
        return;
    }
    ip = (ip << 8) | (cur & 0xFF);

    /* Отправляем 4 пинга с паузой */
    char buf[48];
    buf[0]='P'; buf[1]='I'; buf[2]='N'; buf[3]='G'; buf[4]=' '; buf[5]=0;
    term_push(buf);

    extern void icmp_send_ping(uint32_t);
    extern int icmp_last_reply_ms(void);

    for (int i = 0; i < 4; i++) {
        icmp_send_ping(ip);
        /* Ждём чуть-чуть — 100 тиков PIT = 1 секунда */
        extern volatile uint32_t ticks;
        uint32_t start = ticks;
        while (ticks - start < 100) {
            /* Поллим сеть вручную, чтобы поймать ответ */
            extern void net_poll(void);
            net_poll();
            for (volatile int k = 0; k < 1000; k++);
        }

        int r = icmp_last_reply_ms();
        if (r >= 0) {
            buf[0]='R'; buf[1]='e'; buf[2]='p'; buf[3]='l'; buf[4]='y'; buf[5]=' ';
            buf[6]='s'; buf[7]='e'; buf[8]='q'; buf[9]='='; buf[10]='0'+(r%10);
            buf[11]=0;
            term_push(buf);
        } else {
            term_push("Timeout");
        }
    }
    term_push("Done.");
}

static void cmd_term_dns(const char* arg) {
    /* arg = "example.com" */
    char name[64];
    int i = 0;
    while (arg[i] && arg[i] != ' ' && i < 63) {
        name[i] = arg[i];
        i++;
    }
    name[i] = 0;
    if (i == 0) {
        term_push("usage: dns example.com");
        return;
    }

    extern void net_poll(void);
    extern volatile uint32_t ticks;
    extern int  arp_lookup(uint32_t, uint8_t*);
    extern void arp_send_request(uint32_t);
    extern int  dns_lookup(const char*, uint32_t*);
    extern uint32_t dns_last_ip(void);
    extern int  dns_pending(void);

    /* === Шаг 1: убедиться что gateway MAC в кэше === */
    uint8_t mac[6];
    if (arp_lookup(0x0A000202u, mac) != 0) {
        term_push("Waiting for gateway ARP...");
        arp_send_request(0x0A000202u);
        uint32_t start = ticks;
        while (ticks - start < 300) {   /* 3 секунды */
            net_poll();
            if (arp_lookup(0x0A000202u, mac) == 0) break;
            for (volatile int k = 0; k < 5000; k++);
        }
        if (arp_lookup(0x0A000202u, mac) != 0) {
            term_push("Gateway not responding");
            return;
        }
        term_push("Gateway OK");
    }

    /* === Шаг 2: DNS lookup === */
    char buf[96];
    int n = 0;
    const char* p = "DNS ";
    while (*p) buf[n++] = *p++;
    for (int k = 0; k < i && n < 70; k++) buf[n++] = name[k];
    buf[n] = 0;
    term_push(buf);

    int r = dns_lookup(name, 0);
    if (r != -1) {
        term_push("Send failed");
        return;
    }

    /* Ждём ответа до 5 секунд */
    uint32_t start = ticks;
    while (ticks - start < 500) {
        net_poll();
        if (!dns_pending()) break;
        for (volatile int k = 0; k < 3000; k++);
    }

    if (dns_pending()) {
        term_push("Timeout");
        return;
    }

    uint32_t ip = dns_last_ip();
    n = 0;
    p = "Resolved: ";
    while (*p) buf[n++] = *p++;

    uint8_t a = (ip >> 24) & 0xFF;
    uint8_t b = (ip >> 16) & 0xFF;
    uint8_t c = (ip >> 8)  & 0xFF;
    uint8_t d = ip & 0xFF;
    char tmp[8]; int m;

    m=0; if (a==0) tmp[m++]='0'; else { char t[4]; int k=0; while(a){t[k++]='0'+(a%10);a/=10;} while(k)tmp[m++]=t[--k]; }
    for (int q=0;q<m;q++) buf[n++]=tmp[q]; buf[n++]='.';
    m=0; if (b==0) tmp[m++]='0'; else { char t[4]; int k=0; while(b){t[k++]='0'+(b%10);b/=10;} while(k)tmp[m++]=t[--k]; }
    for (int q=0;q<m;q++) buf[n++]=tmp[q]; buf[n++]='.';
    m=0; if (c==0) tmp[m++]='0'; else { char t[4]; int k=0; while(c){t[k++]='0'+(c%10);c/=10;} while(k)tmp[m++]=t[--k]; }
    for (int q=0;q<m;q++) buf[n++]=tmp[q]; buf[n++]='.';
    m=0; if (d==0) tmp[m++]='0'; else { char t[4]; int k=0; while(d){t[k++]='0'+(d%10);d/=10;} while(k)tmp[m++]=t[--k]; }
    for (int q=0;q<m;q++) buf[n++]=tmp[q];
    buf[n] = 0;
    term_push(buf);
}



static void cmd_term_fetch(const char* arg) {
    /* DEBUG: что пришло */
    extern void serial_puts(const char*);
    serial_puts("[fetch] arg='");
    serial_puts(arg);
    serial_puts("'\n");

    /* Скипаем ведущие пробелы */
    while (*arg == ' ') arg++;

    char host[64];
    int i = 0;
    while (arg[i] && arg[i] != ' ' && i < 63) {
        host[i] = arg[i];
        i++;
    }
    host[i] = 0;

    extern void serial_put_hex(uint32_t);
    serial_puts("[fetch] host='");
    serial_puts(host);
    serial_puts("' i=");
    serial_put_hex((uint32_t)i);
    serial_puts("\n");

    if (i == 0) { term_push("usage: fetch 93.184.216.34"); return; }

    /* Если не IP — резолвим */
    uint32_t ip = 0;
    int dots = 0;
    for (int k = 0; k < i; k++) if (host[k] == '.') dots++;
    uint16_t port = 80;   /* default HTTP */

    /* Разделяем "ip" и ":port" */
    char ip_str[64];
    int ip_len = 0;
    int has_port = 0;
    for (int k = 0; k < i; k++) {
        if (host[k] == ':') { has_port = 1; break; }
        ip_str[ip_len++] = host[k];
    }
    ip_str[ip_len] = 0;

    if (has_port) {
        port = 0;
        for (int k = ip_len + 1; k < i; k++) {
            if (host[k] >= '0' && host[k] <= '9')
                port = port * 10 + (host[k] - '0');
        }
    }

    if (dots == 3) {
        uint32_t cur = 0;
        for (int k = 0; k < ip_len; k++) {
            if (ip_str[k] >= '0' && ip_str[k] <= '9')
                cur = cur * 10 + (ip_str[k] - '0');
            else if (ip_str[k] == '.') { ip = (ip << 8) | (cur & 0xFF); cur = 0; }
        }
        ip = (ip << 8) | (cur & 0xFF);

        /* Дождаться ARP gateway — иначе SYN не уйдёт */
        extern int  arp_lookup(uint32_t, uint8_t*);
        extern void arp_send_request(uint32_t);
        extern void net_poll(void);
        extern volatile uint32_t ticks;
        uint8_t gw[6];
        if (arp_lookup(0x0A000202u, gw) != 0) {
            term_push("Waiting for gateway ARP...");
            arp_send_request(0x0A000202u);
            uint32_t t0 = ticks;
            while (ticks - t0 < 300) {
                net_poll();
                if (arp_lookup(0x0A000202u, gw) == 0) break;
                for (volatile int k = 0; k < 5000; k++);
            }
            if (arp_lookup(0x0A000202u, gw) != 0) {
                term_push("Gateway not responding");
                return;
            }
            term_push("Gateway OK");
        }
    } else {
        /* DNS */
        extern int  dns_lookup(const char*, uint32_t*);
        extern uint32_t dns_last_ip(void);
        extern int  dns_pending(void);
        extern void net_poll(void);
        extern volatile uint32_t ticks;
        extern int  arp_lookup(uint32_t, uint8_t*);
        extern void arp_send_request(uint32_t);

        /* === Шаг 1: дождаться ARP gateway === */
        uint8_t gw_mac[6];
        if (arp_lookup(0x0A000202u, gw_mac) != 0) {
            term_push("Waiting for gateway ARP...");
            arp_send_request(0x0A000202u);
            uint32_t t0 = ticks;
            while (ticks - t0 < 300) {
                net_poll();
                if (arp_lookup(0x0A000202u, gw_mac) == 0) break;
                for (volatile int k = 0; k < 5000; k++);
            }
            if (arp_lookup(0x0A000202u, gw_mac) != 0) {
                term_push("Gateway not responding");
                return;
            }
            term_push("Gateway OK");
        }

        /* === Шаг 2: DNS === */
        term_push("Resolving...");
        dns_lookup(host, 0);
        uint32_t start = ticks;
        while (ticks - start < 500) {
            net_poll();
            if (!dns_pending()) break;
            for (volatile int k = 0; k < 3000; k++);
        }
        if (dns_pending()) { term_push("DNS timeout"); return; }
        ip = dns_last_ip();
        if (ip == 0) { term_push("DNS failed"); return; }
    }

    /* === TCP connect === */
    extern int  tcp_connect(uint32_t, uint16_t);
    extern int  tcp_state(void);
    extern int  tcp_send(const void*, uint32_t);
    extern int  tcp_recv(void*, uint32_t);
    extern void net_poll(void);
    extern volatile uint32_t ticks;

    term_push("Connecting...");
    tcp_connect(ip, port);

    uint32_t start = ticks;
    while (ticks - start < 500) {
        net_poll();
        if (tcp_state() == 2) break;   /* ESTABLISHED */
        for (volatile int k = 0; k < 3000; k++);
    }
    if (tcp_state() != 2) { term_push("Connection failed"); return; }
    term_push("Connected");

    /* === HTTP GET === */
    char req[256];
    int n = 0;
    const char* p = "GET / HTTP/1.1\r\nHost: ";
    while (*p) req[n++] = *p++;
    for (int k = 0; k < i; k++) req[n++] = host[k];
    p = "\r\nConnection: close\r\nUser-Agent: EndixOS/5.0\r\n\r\n";
    while (*p) req[n++] = *p++;

    tcp_send(req, n);

    /* === Читаем ответ === */
    term_push("");
    term_push("=== Response ===");

    static uint8_t buf[2048];
    uint32_t total = 0;
    start = ticks;
    int timeout_count = 0;

    while (ticks - start < 1500 && total < 1500) {
        net_poll();
        int r = tcp_recv(buf, sizeof(buf));
        if (r > 0) {
            for (int k = 0; k < r && total < 1500; k++) {
                char ch = buf[k];
                if (ch == '\r') continue;
                if (ch == '\n') {
                    /* Конец строки — вывести буфер */
                    static char line[120];
                    extern int g_term_line_len;
                    /* Печатаем построчно — упрощённо */
                }
                /* Просто пушим байт в терминал, но строчками */
            }
            /* Печать по строкам, но flush по завершении */
            static char line[128];
            static int ll = 0;
            for (int k = 0; k < r; k++) {
                if (buf[k] == '\r') continue;
                if (buf[k] == '\n') {
                    line[ll] = 0;
                    if (ll > 0) term_push(line);
                    ll = 0;
                } else {
                    if (ll < 120) line[ll++] = buf[k];
                }
            }
            /* Если данных много и нет newline — показываем как есть */
            if (ll > 0 && r > 20) {
                line[ll] = 0;
                term_push(line);
                ll = 0;
            }
            total += r;
            timeout_count = 0;
            if (r == 0) break;
        }
        if (r == 0) break;
        for (volatile int k = 0; k < 1000; k++);
        timeout_count++;
        if (timeout_count > 500) break;
    }
    term_push("=== End ===");
}

static void term_execute(const char* cmd) {
    /* Echo команды */
    char echo[TERM_LINE_LEN];
    int i = 0;
    echo[i++] = '>'; echo[i++] = ' ';
    int k = 0;
    while (cmd[k] && i < TERM_LINE_LEN - 1) echo[i++] = cmd[k++];
    echo[i] = 0;
    term_push(echo);

    if (!cmd[0]) return;

    if (st_eq(cmd, "help"))            { term_print_help();   return; }
    if (st_eq(cmd, "cmatrix"))         { extern void run_cmatrix(void); run_cmatrix(); term_push(""); term_push("matrix ended"); return; }
    if (st_eq(cmd, "net"))             { cmd_term_net();      return; }
    if (st_pfx(cmd, "arp "))           { cmd_term_arp(cmd + 4); return; }
    if (st_pfx(cmd, "ping "))          { cmd_term_ping(cmd + 5); return; }
    if (st_pfx(cmd, "dns "))           { cmd_term_dns(cmd + 4); return; }
    if (st_pfx(cmd, "fetch "))         { cmd_term_fetch(cmd + 6); return; }
    if (st_eq(cmd, "clear"))           { n_lines = 0;          return; }
    if (st_eq(cmd, "uname"))           { term_print_uname();   return; }
    if (st_eq(cmd, "date"))            { term_print_date();    return; }
    if (st_eq(cmd, "exit"))            { wm_close(win_id); win_id = -1; return; }
    if (st_eq(cmd, "logout") || st_eq(cmd, "quit")) {
        extern int g_terminal_wants_exit;
        g_terminal_wants_exit = 1;
        wm_close(win_id); win_id = -1;
        return;
    }
    if (st_eq(cmd, "settings"))        { extern void settings_open(void); settings_open(); return; }
    if (st_eq(cmd, "ls")) {
        term_push("");
        term_push("  FAT16 root:");
        fat16_ls(ls_cb, 0);
        return;
    }
    if (st_pfx(cmd, "echo ")) { term_push(cmd + 5); return; }
    if (st_pfx(cmd, "cat "))  { term_cat(cmd + 4); return; }

    term_push("  unknown command. try 'help'");
}

/* --------- отрисовка окна --------- */

static void terminal_draw(int x, int y, int w, int h, void* user) {
    (void)user;
    /* тёмный фон */
    gfx_box_fill(x, y, w, h, 0x0A0C0C);

    int lh = 14;
    int max_visible = (h - 24) / lh;

    int start = 0;
    if (n_lines > max_visible) start = n_lines - max_visible;

    /* строки истории */
    for (int i = 0; i < max_visible; i++) {
        int idx = start + i;
        if (idx >= n_lines) break;
        int ly = y + 4 + i * lh;
        gfx_puts(x + 8, ly, lines[idx], 0xC8E0E0, 1);
    }

    /* строка ввода */
    int iy = y + h - 20;
    gfx_box_fill(x + 4, iy - 2, w - 8, 18, 0x181C1C);
    gfx_puts(x + 8, iy, ">", 0xFF8A3C, 1);
    gfx_puts(x + 24, iy, input, 0xFFFFFF, 1);

    /* курсор */
    int cx = x + 24 + input_len * 8;
    gfx_box_fill(cx, iy, 2, 10, 0xFFFFFF);
}

/* --------- открыть --------- */

void terminal_open(void) {
    if (win_id >= 0 && wm_is_used(win_id)) {
        wm_raise(win_id);
        wm_render();
        return;
    }
    if (n_lines == 0) {
        term_push("EndixOS Terminal v5.0");
        term_push("Type 'help' for commands.");
        term_push("");
        term_push("  To exit terminal: 'exit' command");
        term_push("  To leave desktop: ESC or F12 or Ctrl+Q");
        term_push("  Tab switches windows, Ctrl+W closes window");
        term_push("");
    }
    input_len = 0;
    input[0] = 0;

    win_id = wm_create("Terminal", 260, 120, 560, 420, terminal_draw, 0);
    wm_render();
}

/* --------- клавиши внутри терминала --------- */

void terminal_handle_key(int c) {
    if (win_id < 0) return;

    if (c == '\n') {
        input[input_len] = 0;
        term_execute(input);
        input_len = 0;
        input[0] = 0;
        wm_render();
        return;
    }
    if (c == '\b') {
        if (input_len > 0) {
            input_len--;
            input[input_len] = 0;
            wm_render();
        }
        return;
    }
    if (c >= 32 && c < 127) {
        if (input_len < TERM_LINE_LEN - 1) {
            input[input_len++] = (char)c;
            input[input_len] = 0;
            wm_render();
        }
    }
    /* Tab и ESC НЕ перехватываем — пусть работают глобально */
}

int terminal_win_id(void)  { return win_id; }
int terminal_is_open(void) { return (win_id >= 0 && wm_is_used(win_id)); }
