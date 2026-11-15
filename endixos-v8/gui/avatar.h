#ifndef ENDIX_AVATAR_H
#define ENDIX_AVATAR_H
#include "../kernel/types.h"
/* Аватарка пользователя: силуэт или один из эмодзи на цветном фоне. Показывается на экране входа и в Настройках. */
#define AVATAR_EMOJI 15
#define AVATAR_COLORS 8
int  avatar_get(void);                      /* 0 - силуэт, 1..AVATAR_EMOJI - эмодзи */
void avatar_set(int a);
int  avatar_color(void);                    /* 0..AVATAR_COLORS-1 */
void avatar_set_color(int c);
u32  avatar_color_rgb(int c);
void avatar_draw(i32 cx, i32 cy, i32 r);    /* круг с центром (cx,cy) и радиусом r */
void avatar_draw_as(int a, int c, i32 cx, i32 cy, i32 r);
#endif
