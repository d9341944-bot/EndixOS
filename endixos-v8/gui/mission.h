#ifndef ENDIX_MISSION_H
#define ENDIX_MISSION_H

#include "../kernel/types.h"

/* Снимки окон для Mission Control: каждое окно запоминается отдельно и потом рисуется
 * в любом размере. Если места в пуле мало, снимок хранится вдвое меньше. */
void mc_reset(void);
int  mc_capture(int id, i32 x, i32 y, i32 w, i32 h, i32 r);          /* 1, если окно запомнено */

/* Рисует снимок окна id в прямоугольнике (left, top, wd, ht) - в 1/256 пикселя - с прозрачностью alpha 0..1024. */
void mc_draw(int id, i32 left, i32 top, i32 wd, i32 ht, i32 alpha);

#endif
