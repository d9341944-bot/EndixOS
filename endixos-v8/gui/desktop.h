#ifndef ENDIX_DESKTOP_H
#define ENDIX_DESKTOP_H

#include "../kernel/types.h"

/* Рабочий стол: значки из ~/Desktop и контекстное меню по правой кнопке. */

void desktop_draw(i32 w, i32 h);                     /* значки; рисуются поверх обоев, под окнами */
int  desktop_press(i32 x, i32 y, u32 ticks);         /* левый клик по пустому месту: выбор значка. Возвращает узел для открытия при двойном клике или -1 */
void desktop_deselect(void);

enum { DK_ACT_NONE, DK_ACT_NEWFOLDER, DK_ACT_WALLPAPER, DK_ACT_APPEAR, DK_ACT_GLASS, DK_ACT_EDITWIDGETS, DK_ACT_REMOVEWIDGET };

void desktop_ctx_open(i32 x, i32 y);
void desktop_ctx_open_widget(i32 x, i32 y, int idx);   /* меню виджета: Remove, Edit Widgets */
int  desktop_ctx_widget(void);                          /* для какого виджета открыто меню */
void desktop_ctx_close(void);
int  desktop_ctx_visible(void);                      /* открыто или ещё исчезает */
int  desktop_ctx_is_open(void);
int  desktop_ctx_hit(i32 x, i32 y);                  /* курсор внутри меню */
void desktop_ctx_hover(i32 x, i32 y);
int  desktop_ctx_click(i32 x, i32 y);                /* DK_ACT_*, меню закрывается */
void desktop_ctx_draw(void);                         /* с плавным появлением */
int  desktop_tick(u32 dt);                           /* 1, пока идёт анимация */

#endif
