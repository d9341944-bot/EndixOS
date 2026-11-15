#ifndef ENDIX_CALC_H
#define ENDIX_CALC_H
#include "../kernel/types.h"
#define CALC_W 280
#define CALC_H 430                 /* высота содержимого */
void calc_reset(void);
int  calc_key(i32 key);            /* 1 - нужна перерисовка */
int  calc_click(i32 x, i32 y);     /* координаты относительно содержимого окна */
void calc_draw(i32 x, i32 y, i32 w, i32 h);
#endif
