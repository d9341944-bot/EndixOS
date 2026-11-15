#ifndef ENDIX_BATTERY_H
#define ENDIX_BATTERY_H
#include "../kernel/types.h"
/* Батарея через EC: заряд в процентах и признак адаптера читаются из двух-трёх байтов EC. Смещения у каждой модели свои,
 * поэтому они задаются командой "battery set" (и сохраняются в prefs), пока не будет вшита таблица для известных моделей. */
int  bat_configured(void);
int  bat_percent(void);                         /* 0..100 или -1, если неизвестно */
int  bat_ac(void);                              /* 1 - адаптер подключён, 0 - нет, -1 - неизвестно */
void bat_config(int pct_off, int ac_off, int ac_mask);   /* pct_off < 0 - выключить; ac_off < 0 - без признака адаптера */
void bat_get_config(int *pct_off, int *ac_off, int *ac_mask);
int  bat_autodetect(void);                      /* ищет блок данных батареи в EC; 1 - найден однозначно */
int  bat_autodetect_info(int *count);           /* смещение найденной пары (или -1) и число кандидатов */
int  bat_poll(u32 ticks);                       /* раз в ~5 с читает EC; 1 - значение изменилось */
void bat_draw(i32 x, i32 y, u32 ink);           /* значок и проценты; возвращает ничего, ширина - bat_width() */
i32  bat_width(void);
#endif
