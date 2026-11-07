#ifndef ENDIX_INSTALLER_H
#define ENDIX_INSTALLER_H
#include "../kernel/types.h"

/* Графический установщик: окно на весь экран поверх рабочего стола. */
void installer_open(void);
int  installer_active(void);
void installer_draw(void);                       /* рисует весь кадр (без fb_flip) */
int  installer_mouse(i32 x, i32 y, int btn);     /* 1 - нужна перерисовка */
int  installer_key(int c);                       /* 1 - нужна перерисовка */
int  installer_tick(u32 ticks);                  /* 1 - нужна перерисовка (анимация, мигание курсора, сама установка) */
#endif
