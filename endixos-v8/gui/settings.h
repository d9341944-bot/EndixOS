#ifndef ENDIX_SETTINGS_H
#define ENDIX_SETTINGS_H

#include "../kernel/types.h"
#include "wm.h"

/* Все координаты - от левого верхнего угла области содержимого окна (под заголовком). */

void settings_init(const sysinfo_t *info);          /* рисует стартовые обои и превью */
void settings_set_wallpaper(int id);                /* меняет обои на экране */
int  settings_wallpaper(void);

void settings_draw(i32 x, i32 y, i32 w, i32 h);
int  settings_click(i32 rx, i32 ry);                /* 1, если что-то изменилось */

/* Панель "О системе", общая для окна About и страницы About в Settings. */
void about_draw(i32 x, i32 y);

/* Окно About в стиле "Об этом Mac". about_window_click: 1 - нажата More Info (открыть Settings). */
void about_window_draw(i32 x, i32 y, i32 w, i32 h);
int  about_window_click(i32 rx, i32 ry, i32 w);
void settings_show_about(void);

#endif
