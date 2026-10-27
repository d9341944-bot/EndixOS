#ifndef ENDIX_ICONS_H
#define ENDIX_ICONS_H

#include "../kernel/types.h"

#define ICON_S 128      /* сторона исходного значка в пикселях */

/* Номера значков: 0..4 - приложения по порядку enum в wm.c, 5 - Launchpad. */
enum {
    ICON_LAUNCHPAD = 5,
    ICON_SIDE_HOME, ICON_SIDE_DESKTOP, ICON_SIDE_DOC, ICON_SIDE_DOWN, ICON_SIDE_PHOTO, ICON_SIDE_MUSIC, ICON_SIDE_DISK,   /* без плитки, цвет акцента */
    ICON_TILE_WALL, ICON_TILE_APPEAR, ICON_TILE_GLASS,
    ICON_DESK_FOLDER, ICON_DESK_FILE,                                                                                          /* значки на рабочем столе */                                                                                         /* плитки для Settings */
    ICON_COUNT
};

/* Рисует значок id в буфер ICON_S x ICON_S: ARGB, альфа в старшем байте, цвет НЕ умножен на альфу.
 * Всё считается через поля расстояний с точным сглаживанием краёв, без float. */
void icon_render(int id, u32 *argb, u32 accent, int dark);

/* Рисует значок любого размера в кадр (с кэшем и уменьшенными копиями); кэш обновляется при смене темы/акцента. */
void icon_draw(int id, i32 x, i32 y, i32 size);

#endif
