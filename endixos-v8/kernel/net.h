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

/* Загрузка страницы в память (для браузера). Тело - в buf (всегда с нулём в конце), до cap-1 байт.
 * Перенаправления http:// выполняются сами; редирект на https: ошибка. */
typedef struct { int state; int status; u32 len; int truncated; int ctype; char err[110]; char url[256]; } net_fetch_t;
enum { NF_IDLE, NF_LOADING, NF_DONE, NF_ERROR };      /* ctype: 0 html, 1 text/plain, 2 другое (картинка, файл) */
int  net_fetch(const char *url, u8 *buf, u32 cap);
void net_fetch_info(net_fetch_t *f);
void net_fetch_abort(void);
void net_url_join(const char *base, const char *ref, char *out, u32 cap);

int  net_job_active(void);
void net_stop(void);                                 /* прервать ping (Ctrl+C), выводит итог */

/* Асинхронное разрешение имени для других модулей: done(ip, ok, arg) вызывается один раз. */
typedef void (*net_resolve_cb)(u32 ip, int ok, void *arg);
int  net_resolve(const char *host, net_resolve_cb done, void *arg);   /* 0 - запущено; -1 - занято */

u32  net_ip_parse(const char *s, int *ok);
void net_ip_fmt(u32 ip, char *buf);                  /* "a.b.c.d", до 16 байт */
int  net_dhcp_state(void);
int  net_changed(void);                              /* 1, если состояние (адрес, связь) изменилось с прошлого вызова */

#endif
