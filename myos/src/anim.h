#ifndef ANIM_H
#define ANIM_H
#include <stdint.h>

/* Глобальные ID анимаций */
#define ANIM_WIN_OPEN    0
#define ANIM_WIN_CLOSE   1
#define ANIM_MENU_OPEN   2
#define ANIM_WALL_SWITCH 3
#define ANIM_MAX         8

void  anim_init(void);
void  anim_start(int id, uint32_t duration_ticks);
float anim_progress(int id);
int   anim_active(int id);
void  anim_tick(void);

/* Easing */
float ease_out_cubic(float t);
float ease_in_out_cubic(float t);
float ease_in_quad(float t);

#endif
