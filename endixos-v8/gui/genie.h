#ifndef ENDIX_GENIE_H
#define ENDIX_GENIE_H

#include "../kernel/types.h"

/* Анимация "джинн": окно втягивается в значок Dock и возвращается обратно. */

/* Запоминает окно (x,y,w,h) из текущего кадра теневого буфера. r - радиус углов, shadow - сила тени 0..255. */
void genie_capture(i32 x, i32 y, i32 w, i32 h, i32 r, u32 shadow);

/* Рисует кадр поверх теневого буфера. (tx,ty) - центр значка, iw - его ширина,
 * p: 0 - окно на месте, 1024 - окно целиком в Dock. */
void genie_draw(i32 tx, i32 ty, i32 iw, i32 p);

/* Окно растёт или уменьшается вокруг своего центра: s 1024 = натуральный размер, alpha 0..1024. */
void genie_zoom(i32 s, i32 alpha);

#endif
