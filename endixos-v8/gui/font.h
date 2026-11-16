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
/* Вертикальный диапазон, за пределы которого текст не рисуется (для прокручиваемых областей). */
void font_set_clip(i32 y0, i32 y1);
void font_clear_clip(void);
i32 font_width(const char *s, int style);

/* UTF-8 помощники для редакторов: позиции всегда на границах символов. */
static inline int utf8_put(char *d, int cp)            /* записывает 1-2 байта, возвращает длину */
{
    if (cp < 0x80) { d[0] = (char)cp; return 1; }
    d[0] = (char)(0xC0 | (cp >> 6)); d[1] = (char)(0x80 | (cp & 63)); return 2;
}
static inline int utf8_clen(const char *s)             /* длина символа, начинающегося в s */
{
    unsigned char c = (unsigned char)s[0];
    if (c >= 0xC0 && ((unsigned char)s[1] & 0xC0) == 0x80) return 2;
    return 1;
}
static inline int utf8_prev(const char *s, int pos)    /* начало символа перед pos */
{
    if (pos <= 0) return 0;
    pos--;
    if (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80 && (unsigned char)s[pos - 1] >= 0xC0) pos--;
    return pos;
}
static inline int utf8_cp(const char *s)               /* кодпоинт символа в s (2 байта максимум) */
{
    if (utf8_clen(s) == 2) return ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
    return (unsigned char)s[0];
}
static inline int utf8_append(char *buf, int *n, int cap, int cp)   /* дописать символ, если влезает */
{
    char b[3]; int k = utf8_put(b, cp);
    if (*n + k > cap) return 0;
    for (int i = 0; i < k; i++) buf[(*n)++] = b[i];
    buf[*n] = 0; return 1;
}
static inline void utf8_pop(char *buf, int *n)                      /* убрать последний символ */
{
    if (*n <= 0) return;
    *n = utf8_prev(buf, *n); buf[*n] = 0;
}
static inline int is_text_cp(int cp)                   /* печатаемые: ASCII и кириллица */
{
    return (cp >= 32 && cp < 127) || (cp >= 0x410 && cp <= 0x44F) || cp == 0x401 || cp == 0x451;
}

#endif
