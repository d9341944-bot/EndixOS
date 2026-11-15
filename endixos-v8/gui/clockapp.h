#ifndef ENDIX_CLOCKAPP_H
#define ENDIX_CLOCKAPP_H
#include "../kernel/types.h"
#define CLK_W 360
#define CLK_H 400                  /* высота содержимого */
/* Секундомер и таймер. Время считается по тикам таймера (100 Гц). */
int  clk_key(i32 key);
int  clk_click(i32 x, i32 y, u32 ticks);
int  clk_tick(u32 ticks, int visible);   /* 1 - перерисовать */
int  clk_fired(void);                    /* 1 один раз, когда таймер дошёл до нуля */
void clk_draw(i32 x, i32 y, i32 w, i32 h);
#endif
