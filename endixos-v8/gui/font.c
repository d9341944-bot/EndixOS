/* Вывод текста готовыми глифами с альфа-смешиванием. */
#include "font.h"
#include "fb.h"

static int glyph_index(u32 cp)
{
    if (cp >= 0x20 && cp <= 0x7E)   return (int)(cp - 0x20);
    if (cp >= 0x410 && cp <= 0x44F) return 95 + (int)(cp - 0x410);
    if (cp == 0x401)                return 159;
    if (cp == 0x451)                return 160;
    return '?' - 0x20;
}

/* Декодирует один символ UTF-8, двигает указатель. */
static u32 next_cp(const char **ps)
{
    const u8 *s = (const u8 *)*ps;
    u32 cp;
    if (s[0] < 0x80) {
        cp = s[0];
        *ps += 1;
    } else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = ((u32)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *ps += 2;
    } else {
        cp = '?';                 /* 3-4 байтовые и битые последовательности */
        *ps += 1;
        while ((((const u8 *)*ps)[0] & 0xC0) == 0x80)
            *ps += 1;
    }
    return cp;
}

i32 font_draw(i32 x, i32 y, const char *s, u32 color, int style)
{
    i32 base = y + font_ascent[style];
    while (*s) {
        const font_glyph_t *g = &font_glyphs[style][glyph_index(next_cp(&s))];
        const u8 *a = &font_alpha[g->off];
        for (i32 j = 0; j < g->h; j++) {
            for (i32 i = 0; i < g->w; i++) {
                u32 al = a[j * g->w + i];
                if (al)
                    fb_blend_pixel(x + g->x + i, base + g->y + j, color, al);
            }
        }
        x += g->adv;
    }
    return x;
}

i32 font_width(const char *s, int style)
{
    i32 w = 0;
    while (*s)
        w += font_glyphs[style][glyph_index(next_cp(&s))].adv;
    return w;
}
