#ifndef ENDIX_WALLPAPER_H
#define ENDIX_WALLPAPER_H

#include "../kernel/types.h"

#define WALL_COUNT 12

const char *wallpaper_name(int id);

/* Рисует обои id в буфер dst размером w x h (шаг строки = w). Только целочисленная
 * математика, без делений в цикле по пикселям: полный экран рисуется быстро. */
void wallpaper_paint(int id, u32 *dst, u32 w, u32 h);

#endif
