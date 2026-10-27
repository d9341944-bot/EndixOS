#ifndef ENDIX_NET_H
#define ENDIX_NET_H

#include "types.h"

/* Сетевой стек: Ethernet, ARP, IPv4, ICMP (ping), UDP, DHCP-клиент и DNS. Работает опросом:
 * драйвер сетевой карты отдаёт кадры в net_rx, а сам стек отправляет их через функцию tx.
 * IP-адреса - u32 в обычном порядке (a.b.c.d = a<<24 | b<<16 | c<<8 | d). */

typedef int (*net_tx_fn)(const void *frame, u32 len);
typedef void (*net_log_fn)(const char *line);        /* строка вывода фоновой команды (ping, nslookup) */

void net_init(const u8 mac[6], net_tx_fn tx);
void net_set_log(net_log_fn f);
void net_rx(const u8 *frame, u32 len);
void net_tick(u32 ticks);                            /* таймеры: DHCP, DNS, ping; вызывать часто (PIT_HZ = 100) */

typedef struct { u8 mac[6]; u32 ip, mask, gw, dns; int up, present; } net_info_t;
void net_info(net_info_t *i);

int  net_ping(const char *host, int count);          /* 0 - запущено; -1 - занято или нет сети (причина в логе) */
int  net_nslookup(const char *host);
int  net_http_get(const char *url, int show_headers);   /* http://host[:port]/path; тело и ошибки идут в лог */
int  net_job_active(void);
void net_stop(void);                                 /* прервать ping (Ctrl+C), выводит итог */

/* Асинхронное разрешение имени для других модулей: done(ip, ok, arg) вызывается один раз. */
typedef void (*net_resolve_cb)(u32 ip, int ok, void *arg);
int  net_resolve(const char *host, net_resolve_cb done, void *arg);   /* 0 - запущено; -1 - занято */

u32  net_ip_parse(const char *s, int *ok);
void net_ip_fmt(u32 ip, char *buf);                  /* "a.b.c.d", до 16 байт */
int  net_changed(void);                              /* 1, если состояние (адрес, связь) изменилось с прошлого вызова */

#endif
