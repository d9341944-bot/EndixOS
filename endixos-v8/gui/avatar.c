#include "avatar.h"
#include "fb.h"
#include "icons.h"

static int av, avc;                         /* выбранная аватарка и цвет фона */

static const u32 PAL[AVATAR_COLORS] = { 0x8E8E96, 0x0A84FF, 0x7D5CF0, 0xFF5E9A, 0xFF5A4F, 0xFF9F0A, 0x30C060, 0x1EC8C8 };

int  avatar_get(void) { return av; }
void avatar_set(int a) { av = a < 0 || a > AVATAR_EMOJI ? 0 : a; }
int  avatar_color(void) { return avc; }
void avatar_set_color(int c) { avc = c < 0 || c >= AVATAR_COLORS ? 0 : c; }
u32  avatar_color_rgb(int c) { return PAL[c < 0 || c >= AVATAR_COLORS ? 0 : c]; }

static int isq(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }

void avatar_draw_as(int a, int c, i32 cx, i32 cy, i32 r)
{
    u32 col = PAL[c < 0 || c >= AVATAR_COLORS ? 0 : c];
    if (a <= 0) {                                         /* силуэт человека */
        fb_fill_circle(cx, cy, r, c ? col : RGB(142, 142, 150));
        fb_fill_circle(cx, cy - r / 5, r * 3 / 10, RGB(236, 236, 240));
        i32 bcy = cy + r * 9 / 10, rx = r * 7 / 10, ry = r * 6 / 10;
        for (i32 y = cy + r / 8; y < cy + r; y++) {
            i32 dy = y - cy, ha = isq(r * r - dy * dy), db = y - bcy, hb = 0;
            if (db > -ry && db < ry) hb = rx * isq(ry * ry - db * db) / ry;
            i32 hw = ha < hb ? ha : hb;
            if (hw > 0) fb_fill_rect(cx - hw, y, 2 * hw, 1, RGB(236, 236, 240));
        }
        return;
    }
    fb_fill_circle(cx, cy, r, col);
    i32 sz = r * 3 / 2;
    icon_draw(ICON_EMOJI0 + a - 1, cx - sz / 2, cy - sz / 2, sz);
}

void avatar_draw(i32 cx, i32 cy, i32 r) { avatar_draw_as(av, avc, cx, cy, r); }
