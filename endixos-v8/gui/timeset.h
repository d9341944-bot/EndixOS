#ifndef ENDIX_TIMESET_H
#define ENDIX_TIMESET_H
#include "../kernel/types.h"

/* Дата и время: ручная установка, часовой пояс, синхронизация по интернету (заголовок Date у HTTPS-ответа). */
void ts_note(u32 h, u32 m, u32 d, u32 mo, u32 y);       /* wm_set_time сообщает текущее время */
void ts_now(u32 *h, u32 *m, u32 *d, u32 *mo, u32 *y);
int  ts_tz(void);                                       /* часовой пояс, минуты от UTC (-720..840) */
void ts_set_tz(int minutes);
int  ts_auto(void);                                     /* 1 - ставить время по интернету при включении */
void ts_set_auto(int on);
int  ts_configured(void);
void ts_load(int tz_minutes, int autosync);

enum { TS_IDLE, TS_BUSY, TS_OK, TS_FAIL };
int  ts_state(void);
const char *ts_msg(void);
void ts_sync(void);                                     /* запускает загрузку и установку времени */
void ts_poll(void);                                     /* продвигает синхронизацию; зовётся часто */
void ts_tick(u32 uptime_seconds);                       /* раз в секунду: автосинхронизация */
int  ts_apply(u32 y, u32 mo, u32 d, u32 h, u32 mi);     /* ручная установка (локальное время); 0 - успех */

/* разбор "Sun, 15 Nov 2026 11:55:01 GMT" + пояс -> локальные поля; 0 - успех */
int  ts_from_http_date(const char *s, int tz_minutes, u32 *y, u32 *mo, u32 *d, u32 *h, u32 *mi, u32 *sec);
int  ts_days_in_month(u32 y, u32 mo);
#endif
