#ifndef ENDIX_BROWSER_H
#define ENDIX_BROWSER_H

#include "../kernel/types.h"

/* Окно браузера: адресная строка, назад/вперёд/обновить, страница (HTML без CSS и скриптов), прокрутка.
 * Только http:// (HTTPS потребует TLS). Встроенные страницы: endix://home, endix://about. */

#define BROWSER_W 840
#define BROWSER_H 560            /* вместе с заголовком окна */

void browser_init(void);
void browser_open(const char *url);                 /* адрес или поисковый запрос, как набрано в строке */
void browser_set_size(i32 w, i32 h);                /* область содержимого окна */
void browser_draw(i32 x, i32 y, int focused);       /* x, y - левый верхний угол области содержимого */
int  browser_click(i32 rx, i32 ry, u32 ticks);      /* левая кнопка нажата; 1 - нужна перерисовка */
int  browser_mouse(i32 rx, i32 ry, int btn, int over);   /* движение: подсветка, ссылка под курсором, перетаскивание полосы */
int  browser_key(i32 key, i32 ctrl);
int  browser_wheel(i32 delta);
int  browser_tick(u32 ticks);                       /* 1 - сцену нужно перерисовать */
const char *browser_title(void);                    /* заголовок страницы или "" */

#endif
