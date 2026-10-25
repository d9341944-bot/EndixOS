/* Общие элементы интерфейса: кнопки, переключатели, карточки, значки. */
#include "ui.h"
#include "fb.h"
#include "font.h"
#include "theme.h"

void ui_hline(i32 x, i32 y, i32 w)
{
    fb_blend_rect(x, y, w, 1, theme_is_dark() ? RGB(255, 255, 255) : RGB(0, 0, 0), theme_is_dark() ? 22 : 18);
}

void ui_group(i32 x, i32 y, i32 w, i32 h)
{
    fb_fill_round_rect(x - 1, y - 1, w + 2, h + 2, 11, theme_is_dark() ? RGB(255, 255, 255) : RGB(0, 0, 0),
                       theme_is_dark() ? 18 : 16);
    fb_fill_round_rect(x, y, w, h, 10, theme()->card, 255);
}

void ui_button(i32 x, i32 y, i32 w, i32 h, const char *label, int enabled, int primary)
{
    const theme_t *t = theme();
    int dark = theme_is_dark();
    fb_fill_round_rect(x, y + 1, w, h, 6, RGB(0, 0, 0), dark ? 70 : 30);              /* нижняя кромка */
    fb_fill_round_rect(x - 0, y, w, h, 6, RGB(0, 0, 0), dark ? 90 : 38);              /* обводка */
    u32 fill = primary && enabled ? theme_accent() : (dark ? RGB(98, 98, 106) : RGB(255, 255, 255));
    fb_fill_round_rect(x + 1, y + 1, w - 2, h - 2, 5, fill, 255);
    u32 ink = primary && enabled ? RGB(255, 255, 255) : (enabled ? t->text : t->sub);
    font_draw(x + (w - font_width(label, FONT_REGULAR)) / 2, y + (h - font_height[FONT_REGULAR]) / 2,
              label, ink, FONT_REGULAR);
}

void ui_chevron(i32 cx, i32 cy, int dir, u32 color)
{
    for (i32 k = 0; k <= 4; k++) {
        i32 x = cx - dir * (k - 2);
        fb_fill_rect(x, cy - k, 2, 2, color);
        fb_fill_rect(x, cy + k, 2, 2, color);
    }
}

void ui_switch(i32 x, i32 y, int on)
{
    u32 off = theme_is_dark() ? RGB(88, 88, 96) : RGB(214, 214, 219);
    fb_fill_round_rect(x, y, 40, 24, 12, on ? theme_accent() : off, 255);
    i32 kx = on ? x + 28 : x + 12;
    fb_fill_circle(kx, y + 13, 10, theme_is_dark() ? RGB(40, 40, 46) : RGB(176, 176, 182));
    fb_fill_circle(kx, y + 12, 10, RGB(255, 255, 255));
}

void ui_glyph(int kind, i32 cx, i32 cy, u32 c, u32 cut)
{
    switch (kind) {
    case G_HOME:
        for (i32 r = 0; r < 6; r++)
            fb_fill_rect(cx - r - 1, cy - 7 + r, 2 * r + 3, 1, c);
        fb_fill_rect(cx - 5, cy - 1, 10, 7, c);
        fb_fill_rect(cx - 1, cy + 2, 3, 4, cut);
        break;
    case G_DESKTOP:
        fb_fill_round_rect(cx - 7, cy - 6, 14, 10, 2, c, 255);
        fb_fill_rect(cx - 5, cy - 4, 10, 6, cut);
        fb_fill_rect(cx - 2, cy + 4, 4, 2, c);
        fb_fill_rect(cx - 4, cy + 6, 8, 1, c);
        break;
    case G_DOC:
        fb_fill_round_rect(cx - 5, cy - 7, 11, 14, 2, c, 255);
        fb_fill_rect(cx + 2, cy - 7, 4, 4, cut);
        fb_fill_rect(cx - 3, cy - 1, 7, 1, cut);
        fb_fill_rect(cx - 3, cy + 2, 7, 1, cut);
        break;
    case G_DOWN:
        fb_fill_circle(cx, cy, 7, c);
        fb_fill_rect(cx - 1, cy - 4, 2, 6, cut);
        for (i32 k = 0; k < 3; k++)
            fb_fill_rect(cx - 2 + k, cy + 1 + k - 0, 5 - 2 * k, 1, cut);
        break;
    case G_PHOTO:
        fb_fill_round_rect(cx - 7, cy - 6, 14, 12, 2, c, 255);
        fb_fill_circle(cx - 3, cy - 2, 2, cut);
        for (i32 k = 0; k < 4; k++)
            fb_fill_rect(cx - 1 + k, cy + 4 - k - 1, 2 * (4 - k) - 1 > 0 ? 7 - 2 * k : 1, 1, cut);
        break;
    case G_MUSIC:
        fb_fill_circle(cx - 3, cy + 4, 3, c);
        fb_fill_rect(cx - 1, cy - 6, 2, 10, c);
        fb_fill_rect(cx + 1, cy - 6, 5, 3, c);
        break;
    case G_DISK:
        fb_fill_round_rect(cx - 7, cy - 3, 14, 8, 3, c, 255);
        fb_fill_circle(cx + 4, cy + 1, 1, cut);
        fb_fill_rect(cx - 5, cy + 1, 5, 1, cut);
        break;
    case G_APPEAR:                                   /* круг, наполовину залитый */
        fb_fill_circle(cx, cy, 6, c);
        fb_fill_circle(cx, cy, 4, cut);
        for (i32 j = -4; j <= 4; j++)
            for (i32 i = 0; i <= 4; i++)
                if (i * i + j * j <= 16)
                    fb_fill_rect(cx + i, cy + j, 1, 1, c);
        break;
    case G_INFO:
        fb_fill_circle(cx, cy, 7, c);
        fb_fill_rect(cx - 1, cy - 4, 2, 2, cut);
        fb_fill_rect(cx - 1, cy - 1, 2, 6, cut);
        break;
    }
}

void ui_squircle_icon(int kind, i32 x, i32 y, i32 size, u32 bg)
{
    fb_fill_round_rect(x, y, size, size, size * 3 / 10, bg, 255);
    ui_glyph(kind, x + size / 2, y + size / 2, RGB(255, 255, 255), bg);
}
