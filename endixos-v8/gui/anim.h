#ifndef ENDIX_ANIM_H
#define ENDIX_ANIM_H

#include "../kernel/types.h"

/* Плавные переходы. Значение v лежит в 0..1024 и за каждый тик PIT приближается к цели:
 * быстро в начале и мягко в конце, а смена цели посреди движения идёт без рывка. */
i32 anim_approach(i32 v, i32 target, u32 ticks);

/* Мягкое замедление к концу, p и результат в 0..1024. */
i32 anim_ease_out(i32 p);

/* Окно, меню или панель плавно появляются: участок кадра (x,y,w,h) запоминается ДО рисования,
 * потом anim_region_end смешивает нарисованное с запомненным фоном (alpha 0..1024),
 * сдвигая на dy; всё выше clip_top не рисуется (панель выезжает из-под меню-бара). */
void anim_region_begin(i32 x, i32 y, i32 w, i32 h);
void anim_region_end(i32 dy, i32 alpha, i32 clip_top);

/* Весь экран: запомнить кадр, потом смешать с тем, что нарисовано сверху. */
void anim_screen_save(void);
void anim_screen_blend(i32 alpha);

#endif
