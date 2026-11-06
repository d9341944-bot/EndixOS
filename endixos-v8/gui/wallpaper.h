#ifndef ENDIX_WALLPAPER_H
#define ENDIX_WALLPAPER_H

#include "../kernel/types.h"

#define WALL_COUNT 12                 /* процедурные обои */
#define WALL_RENDERED 5               /* встроенные 3D-сцены: id = WALL_COUNT + k */
#define WALL_PHOTO0 (WALL_COUNT + WALL_RENDERED)   /* фотографии пользователя: id = WALL_PHOTO0 + k */

const char *wallpaper_name(int id);

/* Рисует обои id в буфер dst размером w x h (шаг строки = w). Только целочисленная
 * математика, без делений в цикле по пикселям: полный экран рисуется быстро. */
void wallpaper_paint(int id, u32 *dst, u32 w, u32 h);

#endif
