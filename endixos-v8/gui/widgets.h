#ifndef ENDIX_WIDGETS_H
#define ENDIX_WIDGETS_H

#include "../kernel/types.h"
#include "wm.h"

/* Виджеты рабочего стола в стиле macOS: три размера (малый, средний, большой), галерея-редактор,
 * свободное размещение с притяжением к сетке. Каждый экземпляр - отдельный виджет на столе. */
#define WIDGETS_SAVE 34                   /* байт в настройках: метка + 11 мест по 3 байта */
void widgets_init(const sysinfo_t *info);
void widgets_set_time(u32 hour, u32 min, u32 day, u32 month, u32 year);
void widgets_set_uptime(u32 seconds);
void widgets_draw(int late);          /* late = 1 - режим редактирования, поверх окон */
int  widgets_tick(u32 dt);            /* 1, пока идёт анимация */
int  widgets_visible(void);           /* есть что рисовать или перерисовывать */
int  widgets_hit(i32 x, i32 y);       /* номер виджета под курсором или -1 */
void widgets_remove(int i);
int  widgets_resize(int i, int size); /* size 0/1/2 = малый/средний/большой; 1 - получилось */
void widgets_edit_start(void);
void widgets_edit_stop(void);
int  widgets_edit_active(void);
int  widgets_edit_press(i32 x, i32 y);
void widgets_save(u8 out[WIDGETS_SAVE]);
void widgets_load(const u8 in[WIDGETS_SAVE]);
int  widgets_edit_drag(i32 x, i32 y, int down);   /* перетаскивание в режиме редактирования; 1 - перерисовать */
int  widgets_dragging(void);
int  widgets_pos_dirty(void);         /* 1 один раз после изменения состава или мест */
int  widgets_count(void);
int  widgets_edit_key(i32 key);       /* стрелки/Enter в галерее; 1 - клавиша использована */

#endif
