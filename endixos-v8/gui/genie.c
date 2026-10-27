/* Эффект "джинн" для сворачивания окон. Окно - снимок, который рисуется построчно:
 * каждая строка получает свою ширину и центр, так что низ втягивается в значок Dock раньше верха.
 * Всё в целых числах (без плавающей точки и 64-битного деления). */
#include "genie.h"
#include "fb.h"

#define LV 3                                   /* снимок и две уменьшенные копии: 1/2 и 1/4 */
static u32 lv0[FB_MAX_W * FB_MAX_H];
static u32 lv1[(FB_MAX_W / 2) * (FB_MAX_H / 2)];
static u32 lv2[(FB_MAX_W / 4) * (FB_MAX_H / 4)];
static u32 *lvp[LV] = { lv0, lv1, lv2 };
static i32 lvw[LV], lvh[LV];
static i32 gx, gy, gw, gh;
static u32 gshadow;

#define M2 0x00FF00FFu

/* Смешивание двух полос по 2 канала (цвет и прозрачность): a = 0..256. */
static inline u32 mix4(u32 d, u32 s, u32 a)
{
    u32 ia = 256 - a;
    u32 e = (((d & M2) * ia + (s & M2) * a) >> 8) & M2;
    u32 o = ((((d >> 8) & M2) * ia + ((s >> 8) & M2) * a) >> 8) & M2;
    return e | (o << 8);
}

static i32 sm(i32 t)                           /* сглаженная ступенька, 0..1024 */
{
    if (t <= 0) return 0;
    if (t >= 1024) return 1024;
    i32 q = (t * t) >> 10;
    return (q * (3072 - 2 * t)) >> 10;
}

void genie_capture(i32 x, i32 y, i32 w, i32 h, i32 r, u32 shadow)
{
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    i32 x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    i32 x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    gw = gh = 0;
    if (x1 <= x0 || y1 <= y0 || (x1 - x0) > FB_MAX_W || (y1 - y0) > FB_MAX_H) return;
    const u32 *fbuf = fb_buffer();
    gx = x0; gy = y0; gw = x1 - x0; gh = y1 - y0;
    gshadow = shadow;
    for (i32 j = 0; j < gh; j++)
        for (i32 i = 0; i < gw; i++) {
            u32 a = 255;
            i32 ox = x0 + i - x, oy = y0 + j - y;          /* положение в окне */
            i32 dx = ox < r ? r - 1 - ox : (ox >= w - r ? ox - (w - r) : -1);
            i32 dy = oy < r ? r - 1 - oy : (oy >= h - r ? oy - (h - r) : -1);
            if (dx >= 0 && dy >= 0) {                      /* угол: прозрачность по расстоянию до дуги */
                i32 t = 128 + (r * r - (dx * dx + dy * dy)) * 128 / (2 * r);
                a = t < 0 ? 0 : (t > 255 ? 255 : (u32)t);
            }
            lv0[j * gw + i] = (fbuf[(y0 + j) * W + x0 + i] & 0x00FFFFFFu) | (a << 24);
        }
    lvw[0] = gw; lvh[0] = gh;
    for (int l = 1; l < LV; l++) {                          /* усреднение 2x2 */
        i32 pw = lvw[l - 1], ph = lvh[l - 1];
        lvw[l] = (pw + 1) / 2; lvh[l] = (ph + 1) / 2;
        for (i32 j = 0; j < lvh[l]; j++) {
            i32 j1 = 2 * j + 1 < ph ? 2 * j + 1 : ph - 1;
            for (i32 i = 0; i < lvw[l]; i++) {
                i32 i1 = 2 * i + 1 < pw ? 2 * i + 1 : pw - 1;
                const u32 *a = &lvp[l - 1][(2 * j) * pw], *b = &lvp[l - 1][j1 * pw];
                u32 top = mix4(a[2 * i], a[i1], 128), bot = mix4(b[2 * i], b[i1], 128);
                lvp[l][j * lvw[l] + i] = mix4(top, bot, 128);
            }
        }
    }
}

/* Одна строка снимка на экране: от left до right (1/256 пикселя), sy - строка снимка (8 бит дроби),
 * cv - покрытие строки 0..256, gain - общая прозрачность 0..256. */
static void span_row(u32 *row, i32 W, i32 left, i32 right, i32 sy, int l, i32 cv, u32 gain)
{
    i32 wd = right - left;
    const u32 *src = lvp[l];
    i32 sw = lvw[l], sh = lvh[l];
    i32 syl = sy >> l;
    i32 iy = syl >> 8, fy = syl & 255;
    const u32 *r0 = src + iy * sw;
    const u32 *r1 = src + (iy + 1 < sh ? iy + 1 : sh - 1) * sw;

    u32 step = (((u32)(gw - 1) << 8) << 12) / (u32)wd;      /* пикселей снимка на пиксель экрана, 12 бит дроби */
    i32 xa = left >> 8, xb = (right + 255) >> 8;
    if (xa < 0) xa = 0;
    if (xb > W) xb = W;
    i32 off = (xa << 8) + 128 - left;
    i32 pos = (off >> 8) * (i32)step + (((off & 255) * (i32)step) >> 8);
    i32 maxp = (gw - 1) << 12;
    for (i32 x = xa; x < xb; x++, pos += (i32)step) {
        i32 c = ((x + 1) << 8 < right ? (x + 1) << 8 : right) - (x << 8 > left ? x << 8 : left);
        if (c <= 0) continue;
        i32 q = pos < 0 ? 0 : (pos > maxp ? maxp : pos);
        q >>= l;
        i32 ix = q >> 12, fx = (q >> 4) & 255;
        i32 jx = ix + 1 < sw ? ix + 1 : sw - 1;
        u32 t0 = mix4(r0[ix], r0[jx], (u32)fx), t1 = mix4(r1[ix], r1[jx], (u32)fx);
        u32 px = mix4(t0, t1, (u32)fy);
        u32 al = px >> 24;
        if (c < 256) al = al * (u32)c >> 8;
        if (cv < 256) al = al * (u32)cv >> 8;
        if (gain < 256) al = al * gain >> 8;
        if (al >= 250) row[x] = px & 0x00FFFFFFu;
        else if (al) row[x] = mix4(row[x] & 0x00FFFFFFu, px & 0x00FFFFFFu, al + (al >> 7)) & 0x00FFFFFFu;
    }
}

/* Деформация: у каждой строки окна свой прогресс движения к значку. Нижние строки стартуют раньше и
 * приходят первыми, верхние отстают - получается воронка с плавным S-образным краем, как в macOS.
 * Все параметры в 1/1024; прогресс строки tp = t*(1+k) - (1-s)*k. */
#define GENIE_LAG 560

typedef struct { i32 y, l, r; } grow_t;

static i32 g_gy8, g_gh8, g_sl, g_sr, g_tl, g_tr, g_ty8, g_t;

static grow_t grow(i32 sn)                    /* sn: положение строки в окне 0..1024 */
{
    i32 tp = (g_t * (1024 + GENIE_LAG) - (1024 - sn) * GENIE_LAG) >> 10;
    i32 e = sm(tp);
    grow_t g;
    i32 ys = g_gy8 + ((g_gh8 * sn) >> 10);
    g.y = ys + (((g_ty8 - ys) * e) >> 10);
    g.l = g_sl + (((g_tl - g_sl) * e) >> 10);
    g.r = g_sr + (((g_tr - g_sr) * e) >> 10);
    return g;
}

/* Обратная задача: какой строке окна соответствует экранная строка yc (1/256 пикселя). Функция монотонна. */
static i32 find_sn(i32 yc)
{
    i32 lo = 0, hi = 1024;
    for (int i = 0; i < 11; i++) {
        i32 mid = (lo + hi) >> 1;
        if (grow(mid).y < yc) lo = mid; else hi = mid;
    }
    return hi;
}

void genie_draw(i32 tx, i32 ty, i32 iw, i32 p)
{
    if (!gw || !gh || p >= 1024) return;
    if (p < 0) p = 0;
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    u32 *dst = fb_buffer();
    if (ty < gy + gh + 20) ty = gy + gh + 20;               /* цель всегда ниже окна */
    if (iw < 16) iw = 16;

    g_t = (sm(p) + p) / 2;                              /* ход по времени: мягкий старт и разгон к значку */
    g_gy8 = gy << 8; g_gh8 = gh << 8;
    g_sl = gx << 8; g_sr = (gx + gw) << 8;
    g_tl = (tx - iw / 2) << 8; g_tr = (tx + iw / 2) << 8;
    g_ty8 = ty << 8;

    grow_t top = grow(0), bot = grow(1024);
    if (bot.y - top.y < 3 * 256) return;                    /* всё втянулось в значок */
    i32 gain = 256;
    if (g_t > 820) gain = (1024 - g_t) * 256 / 204;         /* в конце окно растворяется в значке */
    if (gain <= 0) return;

    if (p < 220 && gshadow) {                               /* тень растворяется в начале движения */
        u32 sh = gshadow * (u32)(220 - p) / 220;
        fb_shadow(gx, top.y >> 8, gw, (bot.y - top.y) >> 8, 10, 28, sh);
    }

    for (i32 y = top.y >> 8; y < ((bot.y + 255) >> 8); y++) {
        if (y < 0 || y >= H) continue;
        i32 cv = ((y + 1) << 8 < bot.y ? (y + 1) << 8 : bot.y) - (y << 8 > top.y ? y << 8 : top.y);
        if (cv <= 0) continue;
        if (cv > 256) cv = 256;
        i32 yc = (y << 8) + 128;
        if (yc < top.y) yc = top.y;
        if (yc > bot.y) yc = bot.y;
        i32 sn = find_sn(yc);
        grow_t g = grow(sn);
        i32 sn2 = find_sn(yc + 256 > bot.y ? bot.y : yc + 256);
        i32 left = g.l, right = g.r;
        if (right - left < 256) { i32 m = (left + right) / 2; left = m - 128; right = m + 128; }
        i32 sy = sn * (gh - 1) * 256 / 1024;                /* строка снимка, 8 бит дроби */
        i32 dsy = (sn2 - sn) * (gh - 1) / 4;                /* строк снимка на строку экрана, 256 = 1:1 */
        i32 rx = (gw << 16) / (right - left), r = rx > dsy ? rx : dsy;
        int l = r >= 768 ? 2 : (r >= 384 ? 1 : 0);
        span_row(dst + (u32)y * (u32)W, W, left, right, sy, l, cv, (u32)gain);
    }
}

/* Появление и исчезновение окна: снимок растёт или уменьшается вокруг своего центра.
 * s - масштаб (1024 = как есть), alpha 0..1024. */
void genie_zoom(i32 s, i32 alpha)
{
    if (!gw || !gh || alpha <= 0) return;
    if (s < 512) s = 512;
    if (s > 1536) s = 1536;
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    u32 *dst = fb_buffer();
    u32 gain = alpha >= 1024 ? 256 : (u32)alpha >> 2;
    i32 wd = gw * s / 4, ht = gh * s / 4;                   /* 1/256 пикселя */
    i32 cxr = (gx << 8) + (gw << 7), cyr = (gy << 8) + (gh << 7);
    i32 left = cxr - wd / 2, top = cyr - ht / 2, bot = top + ht;
    if (wd < 256 || ht < 256) return;

    if (gshadow)
        fb_shadow(left >> 8, top >> 8, wd >> 8, ht >> 8, 10, 28, gshadow * gain >> 8);

    for (i32 y = top >> 8; y < ((bot + 255) >> 8); y++) {
        if (y < 0 || y >= H) continue;
        i32 cv = ((y + 1) << 8 < bot ? (y + 1) << 8 : bot) - (y << 8 > top ? y << 8 : top);
        if (cv <= 0) continue;
        if (cv > 256) cv = 256;
        i32 yc = (y << 8) + 128;
        if (yc < top) yc = top;
        if (yc > bot) yc = bot;
        i32 nm = (yc - top) * (gh - 1), dn = ht;
        i32 sy = (nm / dn) * 256 + (nm % dn) * 256 / dn;
        span_row(dst + (u32)y * (u32)W, W, left, left + wd, sy, 0, cv, gain);
    }
}
