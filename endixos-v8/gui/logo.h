#ifndef ENDIX_LOGO_H
#define ENDIX_LOGO_H

#include "../kernel/types.h"

/* Логотип EndixOS (четырёхлепестковый клевер) в квадрате size x size с левым верхним углом (x, y). */
void logo_draw(i32 x, i32 y, i32 size, u32 color);

#endif
