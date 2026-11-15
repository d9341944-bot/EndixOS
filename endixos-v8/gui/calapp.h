#ifndef ENDIX_CALAPP_H
#define ENDIX_CALAPP_H
#include "../kernel/types.h"
#define CAL_W 360
#define CAL_H 360
void cal_set_today(u32 y, u32 m, u32 d);       /* вызывает wm при смене даты */
int  cal_key(i32 key);
int  cal_click(i32 x, i32 y);
void cal_draw(i32 x, i32 y, i32 w, i32 h);
#endif
