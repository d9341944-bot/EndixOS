#ifndef ENDIX_WIDGETS_H
#define ENDIX_WIDGETS_H

#include "../kernel/types.h"
#include "wm.h"

/* Виджеты рабочего стола: часы, календарь, сведения о системе. Слева, под строкой меню. */
void widgets_init(const sysinfo_t *info);
void widgets_set_time(u32 hour, u32 min, u32 day, u32 month, u32 year);
void widgets_set_uptime(u32 seconds);
void widgets_draw(int late);          /* late = 1 - режим редактирования, поверх окон */
int  widgets_tick(u32 dt);            /* 1, пока идёт анимация */
int  widgets_visible(void);           /* есть что рисовать или перерисовывать */
int  widgets_hit(i32 x, i32 y);       /* номер виджета под курсором или -1 */
void widgets_remove(int i);
void widgets_add(int i);
void widgets_edit_start(void);
void widgets_edit_stop(void);
int  widgets_edit_active(void);
int  widgets_edit_press(i32 x, i32 y);
int  widgets_get_mask(void);          /* какие виджеты включены, по биту на виджет */
void widgets_set_mask(int m);

#endif
