#ifndef ENDIX_CONTROL_H
#define ENDIX_CONTROL_H

#include "../kernel/types.h"

/* Пункт управления (как в macOS): значок в строке меню + панель с плитками и ползунками. */

void cc_draw_icon(i32 x, u32 ink);        /* значок 18x14 с левым краем x, верх строки меню */
void cc_draw(i32 screen_w);               /* панель, если открыта */
void cc_toggle(void);
void cc_close(void);
int  cc_is_open(void);
int  cc_hit(i32 screen_w, i32 x, i32 y);  /* курсор внутри панели */
void cc_press(i32 screen_w, i32 x, i32 y);
int  cc_dragging(void);
void cc_drag(i32 screen_w, i32 x);
void cc_release(void);
int  cc_brightness(void);                 /* 20..100 */
int  cc_volume(void);                     /* 0..100 */

#endif
