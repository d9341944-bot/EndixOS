/* Загрузочный экран: чёрный фон, логотип, название и тонкая полоса прогресса. */
#include "splash.h"
#include "fb.h"
#include "font.h"
#include "logo.h"

void splash_draw(int pct)
{
    i32 w = (i32)fb_width(), h = (i32)fb_height();
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    fb_fill_rect(0, 0, w, h, RGB(0, 0, 0));
    i32 size = (h < w ? h : w) / 5; if (size > 150) size = 150; if (size < 64) size = 64;
    i32 cy = h / 2 - size / 2 - 18;
    logo_draw(w / 2 - size / 2, cy, size, RGB(236, 236, 242));
    const char *name = "EndixOS";
    font_draw(w / 2 - font_width(name, FONT_BOLD) / 2, cy + size + 22, name, RGB(200, 200, 210), FONT_BOLD);
    i32 bw = size * 13 / 10, bh = 4, bx = w / 2 - bw / 2, by = cy + size + 62;
    fb_fill_round_rect(bx, by, bw, bh, 2, RGB(60, 60, 68), 255);
    i32 fw = bw * pct / 100;
    if (fw >= bh) fb_fill_round_rect(bx, by, fw, bh, 2, RGB(235, 235, 240), 255);
    fb_flip();
}
