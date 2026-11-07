/* Снимки окон для Mission Control и их масштабирование с усреднением. */
#include "mission.h"
#include "fb.h"

#define MC_POOL (2400 * 1024)                  /* пикселей: 9.6 МБ */
#define MC_MAX  8
static u32 pool[MC_POOL];
static i32 used;

typedef struct {
    int ok;
    i32 off, w, h;                             /* размер снимка (после возможного уменьшения вдвое) */
    i32 ow, oh;                                /* размер окна на экране */
    i32 cx, cy;                                /* смещение видимой части внутри окна */
    i32 cw, ch;                                /* размер видимой части на экране */
} snap_t;
static snap_t snaps[MC_MAX];

#define M2 0x00FF00FFu
static inline u32 mix4(u32 d, u32 s, u32 a)   /* 4 канала: цвет и прозрачность, a = 0..256 */
{
    u32 ia = 256 - a;
    u32 e = (((d & M2) * ia + (s & M2) * a) >> 8) & M2;
    u32 o = ((((d >> 8) & M2) * ia + ((s >> 8) & M2) * a) >> 8) & M2;
    return e | (o << 8);
}

void mc_reset(void)
{
    used = 0;
    for (int i = 0; i < MC_MAX; i++) snaps[i].ok = 0;
}

int mc_capture(int id, i32 x, i32 y, i32 w, i32 h, i32 r)
{
    if (id < 0 || id >= MC_MAX) return 0;
    snap_t *s = &snaps[id];
    s->ok = 0;
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    i32 x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    i32 x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    if (x1 - x0 < 2 || y1 - y0 < 2) return 0;
    i32 cw = x1 - x0, ch = y1 - y0;
    int half = 0;
    if (used + cw * ch > MC_POOL) {
        half = 1;
        if (used + ((cw + 1) / 2) * ((ch + 1) / 2) > MC_POOL) return 0;
    }
    i32 sw = half ? (cw + 1) / 2 : cw, sh = half ? (ch + 1) / 2 : ch;
    const u32 *fbuf = fb_buffer();
    u32 *dst = pool + used;
    for (i32 j = 0; j < sh; j++)
        for (i32 i = 0; i < sw; i++) {
            u32 acc = 0;
            int cnt = half ? 4 : 1;
            u32 c4[4]; int n = 0;
            for (int q = 0; q < cnt; q++) {
                i32 px = half ? 2 * i + (q & 1) : i, py = half ? 2 * j + (q >> 1) : j;
                if (px >= cw) px = cw - 1;
                if (py >= ch) py = ch - 1;
                i32 ox = x0 + px - x, oy = y0 + py - y;            /* положение в окне */
                u32 a = 255;
                i32 dx = ox < r ? r - 1 - ox : (ox >= w - r ? ox - (w - r) : -1);
                i32 dy = oy < r ? r - 1 - oy : (oy >= h - r ? oy - (h - r) : -1);
                if (dx >= 0 && dy >= 0) {                          /* угол: прозрачность по дуге */
                    i32 t = 128 + (r * r - (dx * dx + dy * dy)) * 128 / (2 * r);
                    a = t < 0 ? 0 : (t > 255 ? 255 : (u32)t);
                }
                c4[n++] = (fbuf[(y0 + py) * W + x0 + px] & 0x00FFFFFFu) | (a << 24);
            }
            if (half) acc = mix4(mix4(c4[0], c4[1], 128), mix4(c4[2], c4[3], 128), 128);
            else acc = c4[0];
            dst[j * sw + i] = acc;
        }
    s->ok = 1; s->off = used; s->w = sw; s->h = sh;
    s->ow = w; s->oh = h; s->cx = x0 - x; s->cy = y0 - y; s->cw = cw; s->ch = ch;
    used += sw * sh;
    return 1;
}

/* Билинейная выборка; координаты в 1/4096 пикселя снимка. */
static inline u32 tap(const u32 *src, i32 sw, i32 sh, i32 sx, i32 sy)
{
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    i32 mx = (sw - 1) << 12, my = (sh - 1) << 12;
    if (sx > mx) sx = mx;
    if (sy > my) sy = my;
    i32 ix = sx >> 12, fx = (sx >> 4) & 255, iy = sy >> 12, fy = (sy >> 4) & 255;
    i32 jx = ix + 1 < sw ? ix + 1 : ix, jy = iy + 1 < sh ? iy + 1 : iy;
    const u32 *r0 = src + iy * sw, *r1 = src + jy * sw;
    return mix4(mix4(r0[ix], r0[jx], (u32)fx), mix4(r1[ix], r1[jx], (u32)fx), (u32)fy);
}

void mc_draw(int id, i32 left, i32 top, i32 wd, i32 ht, i32 alpha)
{
    if (id < 0 || id >= MC_MAX || !snaps[id].ok || alpha <= 0) return;
    const snap_t *s = &snaps[id];
    if (wd < 256 || ht < 256) return;
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    u32 *dst = fb_buffer();
    const u32 *src = pool + s->off;
    u32 gain = alpha >= 1024 ? 256 : (u32)alpha >> 2;

    /* видимая часть окна: смещаем и сжимаем прямоугольник вместе с окном */
    i32 l2 = left + s->cx * wd / s->ow;
    i32 t2 = top + s->cy * ht / s->oh;
    i32 w2 = wd * s->cw / s->ow, h2 = ht * s->ch / s->oh;
    if (w2 < 256 || h2 < 256) return;
    left = l2; top = t2; wd = w2; ht = h2;
    i32 bot = top + ht, right = left + wd;

    /* шаг по снимку (1/4096 пикселя на пиксель экрана) */
    u32 stepx = (((u32)(s->w - 1) << 8) << 12) / (u32)wd;
    u32 stepy = (((u32)(s->h - 1) << 8) << 12) / (u32)ht;
    int taps4 = (stepx >= 5700 || stepy >= 5700);             /* уменьшаем сильнее, чем в 1.4 раза: усредняем 4 выборки */
    i32 ox = (i32)stepx / 4, oy = (i32)stepy / 4;

    for (i32 y = top >> 8; y < ((bot + 255) >> 8); y++) {
        if (y < 0 || y >= H) continue;
        i32 cv = ((y + 1) << 8 < bot ? (y + 1) << 8 : bot) - (y << 8 > top ? y << 8 : top);
        if (cv <= 0) continue;
        if (cv > 256) cv = 256;
        i32 yc = (y << 8) + 128;
        if (yc < top) yc = top;
        if (yc > bot) yc = bot;
        i32 nm = (yc - top) * (s->h - 1), dn = ht;
        i32 sy = (nm / dn) * 4096 + (nm % dn) * 4096 / dn;
        i32 xa = left >> 8, xb = (right + 255) >> 8;
        if (xa < 0) xa = 0;
        if (xb > W) xb = W;
        i32 off = (xa << 8) + 128 - left;
        i32 pos = (off >> 8) * (i32)stepx + (((off & 255) * (i32)stepx) >> 8);
        u32 *row = dst + (u32)y * (u32)W;
        for (i32 x = xa; x < xb; x++, pos += (i32)stepx) {
            i32 c = ((x + 1) << 8 < right ? (x + 1) << 8 : right) - (x << 8 > left ? x << 8 : left);
            if (c <= 0) continue;
            u32 px;
            if (taps4) {
                u32 a = tap(src, s->w, s->h, pos - ox, sy - oy), b = tap(src, s->w, s->h, pos + ox, sy - oy);
                u32 d = tap(src, s->w, s->h, pos - ox, sy + oy), e = tap(src, s->w, s->h, pos + ox, sy + oy);
                px = mix4(mix4(a, b, 128), mix4(d, e, 128), 128);
            } else {
                px = tap(src, s->w, s->h, pos, sy);
            }
            u32 al = px >> 24;
            if (c < 256) al = al * (u32)c >> 8;
            if (cv < 256) al = al * (u32)cv >> 8;
            if (gain < 256) al = al * gain >> 8;
            if (al >= 250) row[x] = px & 0x00FFFFFFu;
            else if (al) row[x] = mix4(row[x] & 0x00FFFFFFu, px & 0x00FFFFFFu, al + (al >> 7)) & 0x00FFFFFFu;
        }
    }
}
