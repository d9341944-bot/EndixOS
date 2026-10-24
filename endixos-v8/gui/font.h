#ifndef ENDIX_FONT_H
#define ENDIX_FONT_H

#include "../kernel/types.h"

#define FONT_REGULAR 0
#define FONT_BOLD    1
#define FONT_MONO    2
#define FONT_TITLE   3          /* крупный заголовок */
#define FONT_STYLES  4
#define FONT_NGLYPHS 161

#define FONT_MONO_ADV 7          /* ширина ячейки моноширинного шрифта */

typedef struct {
    u32 off;                     /* смещение альфа-битмапа в font_alpha */
    u8  w, h;
    i8  x, y;                    /* от начала пера: x вправо, y вверх от базовой линии */
    u8  adv;                     /* продвижение пера */
} font_glyph_t;

extern const u8           font_alpha[];
extern const font_glyph_t font_glyphs[FONT_STYLES][FONT_NGLYPHS];
extern const u8           font_ascent[FONT_STYLES];
extern const u8           font_height[FONT_STYLES];

/* y - верх строки. Строки в UTF-8 (латиница и кириллица). Возвращает x после текста. */
i32 font_draw(i32 x, i32 y, const char *s, u32 color, int style);
i32 font_width(const char *s, int style);

#endif
