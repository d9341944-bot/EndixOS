#ifndef ENDIX_ICONS_H
#define ENDIX_ICONS_H

#include "../kernel/types.h"

#define ICON_S 128      /* сторона исходного значка в пикселях */

/* Номера значков: 0..4 - приложения по порядку enum в wm.c, 5 - Browix, 6 - Launchpad. */
enum {
    ICON_LAUNCHPAD = 6,
    ICON_SIDE_HOME, ICON_SIDE_DESKTOP, ICON_SIDE_DOC, ICON_SIDE_DOWN, ICON_SIDE_PHOTO, ICON_SIDE_MUSIC, ICON_SIDE_DISK,   /* без плитки, цвет акцента */
    ICON_TILE_WALL, ICON_TILE_APPEAR, ICON_TILE_GLASS, ICON_APP_CALC, ICON_APP_NOTES, ICON_APP_CLOCK, ICON_APP_CAL, ICON_APP_MUSIC, ICON_APP_DOOM,
    ICON_DESK_FOLDER, ICON_DESK_FILE, ICON_TRASH, ICON_TRASH_FULL,                                                                                          /* значки на рабочем столе */                                                                                         /* плитки для Settings */
    ICON_EMOJI0,                                                                                                                                            /* 15 эмодзи для аватарок */
    ICON_COUNT = ICON_EMOJI0 + 15
};

/* Стиль значков приложений: обычный, Liquid Glass, одноцветный тёмный (как в macOS 26), тонированный цветом акцента. */
enum { ICON_STYLE_DEFAULT, ICON_STYLE_GLASS, ICON_STYLE_MONO, ICON_STYLE_TINT, ICON_STYLE_COUNT };
int  icon_style(void);
void icon_set_style(int s);

/* Рисует значок id в буфер ICON_S x ICON_S: ARGB, альфа в старшем байте, цвет НЕ умножен на альфу.
 * Всё считается через поля расстояний с точным сглаживанием краёв, без float. */
void icon_render(int id, u32 *argb, u32 accent, int dark);

/* Рисует значок любого размера в кадр (с кэшем и уменьшенными копиями); кэш обновляется при смене темы/акцента. */
void icon_draw(int id, i32 x, i32 y, i32 size);

/* Календарь: настоящая дата на значке (день месяца 1..31, день недели: понедельник = 0). */
void icon_set_date(int day, int wday);

#endif
