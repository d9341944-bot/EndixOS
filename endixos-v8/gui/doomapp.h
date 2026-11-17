#ifndef ENDIX_DOOMAPP_H
#define ENDIX_DOOMAPP_H
#include "../kernel/types.h"
/* Doom: оригинальный движок id Software (GPLv2, через doomgeneric). Нужен WAD-файл (DOOM1.WAD и т.п.) на диске или флешке. */
#define DOOMAPP_W 640
#define DOOMAPP_H 400
void doomapp_init(void);
int  doomapp_key(i32 key);                    /* до запуска: Enter/пробел стартуют игру */
int  doomapp_click(void);
int  doomapp_tick(u32 ticks, int active);     /* active: окно сверху и видно; 1 - нужна перерисовка */
void doomapp_draw(i32 x, i32 y, i32 w, i32 h);
#endif
