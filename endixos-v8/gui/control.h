#ifndef ENDIX_CONTROL_H
#define ENDIX_CONTROL_H

#include "../kernel/types.h"

/* Пункт управления (как в macOS): значок в строке меню + панель с плитками и ползунками. */

void cc_draw_icon(i32 x, u32 ink);        /* значок 18x14 с левым краем x, верх строки меню */
void cc_draw(i32 screen_w);               /* панель, если открыта */
void cc_toggle(void);
void cc_close(void);
int  cc_is_open(void);
void cc_draw_search_icon(i32 x, i32 y, u32 ink);   /* лупа 16x16 с левым верхним углом (x, y) */
int  cc_hit(i32 screen_w, i32 x, i32 y);  /* курсор внутри панели */
enum { CCA_NONE, CCA_SPOTLIGHT, CCA_SETTINGS, CCA_TERMINAL, CCA_FILES, CCA_LOCK, CCA_MISSION };
int  cc_press(i32 screen_w, i32 x, i32 y);   /* CCA_*: что должен выполнить оконный менеджер */
int  cc_dragging(void);
void cc_draw_any(i32 screen_w);
void cc_bounds(i32 screen_w, i32 *x, i32 *y, i32 *w, i32 *h);
void cc_drag(i32 screen_w, i32 x, i32 y);
int  cc_tick(u32 dt);                     /* 1, пока идёт анимация */
int  cc_editing(void);                    /* открыт редактор или галерея */
int  cc_escape(void);                     /* Esc: выйти из редактора; 1 - обработано */
void cc_get_layout(u8 out[16]);           /* состав и порядок плиток для сохранения */
void cc_set_layout(const u8 in[16]);
void cc_release(void);
int  cc_brightness(void);                 /* 20..100 */
int  cc_volume(void);                     /* 0..100 */
void cc_set_brightness(int v);            /* 20..100, как ползунок Display */
void cc_set_volume(int v);

#endif
