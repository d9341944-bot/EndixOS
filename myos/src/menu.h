#ifndef MENU_H
#define MENU_H
#include <stdint.h>

void menu_init(void);
void menu_draw(void);
int  menu_is_open(void);
int  menu_topbar_hit(int x, int y);   /* -1 или индекс меню */
void menu_open_by_index(int idx);
void menu_close(void);
int  menu_handle_key(int c);          /* 1 = обработано */
void menu_click(int x, int y);        /* клик — открыть/закрыть */

#endif
