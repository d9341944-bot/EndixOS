/* Процедурные обои: трёхцветный градиент вдоль заданного направления + мягкое свечение. */
#include "wallpaper.h"
#include "fb.h"

typedef struct {
    const char *name;
    u8 wx, wy;               /* вклад осей в градиент, wx + wy <= 255 */
    u8 c0[3], c1[3], c2[3];  /* три опорных цвета градиента */
    u8 gx, gy, gs;           /* центр свечения (в 1/256 экрана) и его сила */
} wp_t;

#define GRAD_COUNT 2
static const wp_t wps[GRAD_COUNT] = {
    { "Aurora",   128, 127, {40, 30, 120},  {95, 55, 165},  {150, 80, 210},  210,  40,  70 },
    { "Graphite", 100, 155, {18, 18, 22},   {45, 46, 54},   {90, 92, 104},   128, 100,  45 },
};

#define GLOW_LIM 32768          /* квадрат радиуса свечения в единицах 1/256 экрана */

static u32 pal[256];
static u16 tx[FB_MAX_W], ty[FB_MAX_H];
static u32 dx2[FB_MAX_W], dy2[FB_MAX_H];

static void paint_gradient(int id, u32 *dst, u32 w, u32 h)
{
    const wp_t *p = &wps[id];

    for (int t = 0; t < 256; t++) {
        const u8 *a, *b;
        int k;
        if (t < 128) { a = p->c0; b = p->c1; k = t * 2; }
        else         { a = p->c1; b = p->c2; k = (t - 128) * 2; }
        int r = a[0] + ((b[0] - a[0]) * k) / 255;
        int g = a[1] + ((b[1] - a[1]) * k) / 255;
        int bl = a[2] + ((b[2] - a[2]) * k) / 255;
        pal[t] = RGB(r, g, bl);
    }

    for (u32 x = 0; x < w; x++) {
        tx[x] = (u16)(x * p->wx / w);
        i32 d = (i32)(x * 256 / w) - (i32)p->gx;
        dx2[x] = (u32)(d * d);
    }
    for (u32 y = 0; y < h; y++) {
        ty[y] = (u16)(y * p->wy / h);
        i32 d = (i32)(y * 256 / h) - (i32)p->gy;
        dy2[y] = (u32)(d * d);
    }

    for (u32 y = 0; y < h; y++) {
        u32 *row = &dst[y * w];
        for (u32 x = 0; x < w; x++) {
            u32 t = (u32)tx[x] + ty[y];
            if (t > 255) t = 255;
            u32 c = pal[t];

            u32 d2 = dx2[x] + dy2[y];
            if (d2 < GLOW_LIM) {
                u32 gl = ((GLOW_LIM - d2) * p->gs) >> 15;     /* 0..gs */
                u32 r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                r += ((255 - r) * gl) >> 8;
                g += ((255 - g) * gl) >> 8;
                b += ((255 - b) * gl) >> 8;
                c = (r << 16) | (g << 8) | b;
            }
            row[x] = c;
        }
    }
}

/* ================= сцены ================= */
/* Рисуются в виртуальных координатах 1024x768 (VX, VY), поэтому одинаково выглядят и на экране, и в миниатюре. */

static u16 vx[FB_MAX_W], vy[FB_MAX_H];
static u16 bh0[FB_MAX_W], bh1[FB_MAX_W], bid0[FB_MAX_W], bid1[FB_MAX_W];

static u32 hs(u32 a, u32 b)
{
    u32 h = a * 374761393u + b * 668265263u + 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static u32 mixc(u32 a, u32 b, u32 t)                    /* t 0..256 */
{
    if (t > 256) t = 256;
    u32 r = (((a >> 16) & 255) * (256 - t) + ((b >> 16) & 255) * t) >> 8;
    u32 g = (((a >> 8) & 255) * (256 - t) + ((b >> 8) & 255) * t) >> 8;
    u32 bl = ((a & 255) * (256 - t) + (b & 255) * t) >> 8;
    return (r << 16) | (g << 8) | bl;
}

static u32 addc(u32 a, u32 b, u32 k)                    /* a + b*k/256, с насыщением */
{
    u32 r = ((a >> 16) & 255) + ((((b >> 16) & 255) * k) >> 8);
    u32 g = ((a >> 8) & 255) + ((((b >> 8) & 255) * k) >> 8);
    u32 bl = (a & 255) + (((b & 255) * k) >> 8);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (bl > 255) bl = 255;
    return (r << 16) | (g << 8) | bl;
}

static i32 isin(i32 p)                                  /* период 256, -256..256 */
{
    p &= 255;
    if (p < 128) return (p * (128 - p)) >> 4;
    p -= 128;
    return -((p * (128 - p)) >> 4);
}

static u32 smooth(u32 t) { return (t * t * (768 - 2 * t)) >> 16; }   /* t 0..256 */

/* Шум по решётке: x в 1/256, возвращает 0..255. */
static u32 vn1(i32 x, u32 seed)
{
    u32 i = (u32)x >> 8, t = smooth((u32)x & 255);
    u32 a = hs(i, seed) & 255, b = hs(i + 1, seed) & 255;
    return (a * (256 - t) + b * t) >> 8;
}

static u32 vn2(i32 x, i32 y, u32 seed)
{
    u32 ix = (u32)x >> 8, iy = (u32)y >> 8;
    u32 tx_ = smooth((u32)x & 255), ty_ = smooth((u32)y & 255);
    u32 a = hs(ix, iy + seed * 977) & 255, b = hs(ix + 1, iy + seed * 977) & 255;
    u32 c = hs(ix, iy + 1 + seed * 977) & 255, d = hs(ix + 1, iy + 1 + seed * 977) & 255;
    u32 top = (a * (256 - tx_) + b * tx_) >> 8, bot = (c * (256 - tx_) + d * tx_) >> 8;
    return (top * (256 - ty_) + bot * ty_) >> 8;
}

static u32 fbm2(i32 x, i32 y, u32 seed)
{
    return (vn2(x, y, seed) * 4 + vn2(x * 2, y * 2, seed + 1) * 2 + vn2(x * 4, y * 4, seed + 2)) / 7;
}

static u32 vgrad(u32 top, u32 bot, i32 y, i32 y0, i32 y1)   /* вертикальный градиент между y0 и y1 */
{
    if (y <= y0) return top;
    if (y >= y1) return bot;
    return mixc(top, bot, (u32)((y - y0) * 256 / (y1 - y0)));
}

static u32 star(i32 X, i32 Y, u32 seed, u32 density)    /* яркость звезды в точке 0..255 */
{
    u32 h = hs((u32)X, (u32)Y + seed);
    if ((h & 1023) >= density) return 0;
    return 90 + ((h >> 12) & 165);
}

static u32 glow(i32 X, i32 Y, i32 cx, i32 cy, i32 r)    /* мягкий круг 0..256 */
{
    i32 dx = X - cx, dy = Y - cy;
    u32 d2 = (u32)(dx * dx + dy * dy), r2 = (u32)(r * r);
    if (d2 >= r2) return 0;
    u32 t = 256 - d2 * 256 / r2;
    return (t * t) >> 8;
}

static void sc_mountains(u32 *dst, u32 w, u32 h)
{
    static const u32 lay[4] = { 0xC97C98, 0x9A6392, 0x65457F, 0x2A1F4A };
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            i32 X = vx[x], Y = vy[y];
            u32 c = vgrad(RGB(60, 40, 120), RGB(255, 170, 110), Y, 0, 470);
            c = addc(c, RGB(255, 210, 150), glow(X, Y, 700, 400, 260) * 150 >> 8);
            u32 sun = glow(X, Y, 700, 400, 52);
            if (sun) c = mixc(c, RGB(255, 240, 200), sun > 200 ? 256 : sun);
            u32 st = Y < 260 ? star(X, Y, 11, 5) : 0;
            c = addc(c, RGB(255, 255, 255), st / 2);
            for (int k = 0; k < 4; k++) {
                i32 ridge = 440 + k * 85 - (i32)(fbm2(X * 2 + k * 4000, 0, (u32)k + 3) * (330 - k * 55) >> 8);
                if (Y >= ridge) {
                    u32 depth = (u32)(Y - ridge);
                    u32 mc = lay[k];
                    mc = mixc(mc, RGB(255, 170, 130), depth < 40 ? (40 - depth) * 3 : 0);
                    c = mc;
                }
            }
            dst[y * w + x] = c;
        }
}

static void sc_starry(u32 *dst, u32 w, u32 h)
{
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            i32 X = vx[x], Y = vy[y];
            u32 c = vgrad(RGB(5, 8, 32), RGB(34, 40, 96), Y, 0, 640);
            c = addc(c, RGB(60, 70, 150), fbm2(X * 3, Y * 3, 21) > 150 ? (fbm2(X * 3, Y * 3, 21) - 150) : 0);
            u32 st = star(X, Y, 5, 4);
            if (!st) st = star(X / 2 * 2, Y / 2 * 2, 9, 2) / 2;
            c = addc(c, RGB(255, 250, 235), st);
            c = addc(c, RGB(200, 210, 255), glow(X, Y, 780, 160, 150) * 70 >> 8);
            u32 moon = glow(X, Y, 780, 160, 46);
            if (moon) c = mixc(c, RGB(250, 248, 230), moon > 120 ? 256 : moon * 2);
            i32 h1 = 560 + (isin(X / 3 + 20) >> 3) + (isin(X / 5) >> 4);
            i32 h2 = 650 + (isin(X / 4 + 90) >> 3);
            if (Y >= h1) c = RGB(18, 22, 54);
            if (Y >= h2) c = RGB(8, 10, 28);
            dst[y * w + x] = c;
        }
}

static void sc_neon(u32 *dst, u32 w, u32 h)
{
    const i32 HY = 420;
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            i32 X = vx[x], Y = vy[y];
            u32 c;
            if (Y < HY) {
                c = vgrad(RGB(18, 6, 56), RGB(255, 60, 160), Y, 0, HY);
                i32 dx = X - 512, dy = Y - 270;
                if (dx * dx + dy * dy < 170 * 170) {
                    u32 sc = vgrad(RGB(255, 235, 90), RGB(255, 50, 150), Y, 110, 430) ? 0 : 0;
                    (void)sc;
                    c = mixc(RGB(255, 225, 80), RGB(255, 40, 140), (u32)((Y - 100) * 256 / 340));
                    if (Y > 290 && ((Y - 290) / 9) % 2 == 0 && (Y - 290) % 18 < (Y - 290) / 12 + 2) c = vgrad(RGB(18, 6, 56), RGB(255, 60, 160), Y, 0, HY);
                }
                c = addc(c, RGB(255, 80, 200), glow(X, Y, 512, 270, 330) * 60 >> 8);
            } else {
                i32 dz = Y - HY + 1;
                c = vgrad(RGB(90, 10, 110), RGB(10, 4, 30), Y, HY, 768);
                i32 wz = 60000 / dz;                         /* глубина */
                i32 wx = ((X - 512) * wz) >> 6;
                u32 line = 0;
                if ((wz & 255) < (dz < 40 ? 14 : 22) - (dz < 30 ? 4 : 0)) line = 1;
                if ((((wx % 256) + 256) % 256) < 20 + dz / 14 && wz < 3000) line = 1;
                if (((wx % 256) + 256) % 256 > 256 - 20 - dz / 14 && wz < 3000) line = 1;
                if (line) c = addc(c, RGB(0, 220, 255), dz < 20 ? 90 : 220);
                c = addc(c, RGB(255, 40, 160), glow(X, Y, 512, HY, 200) * 90 >> 8);
            }
            dst[y * w + x] = c;
        }
}

static void sc_waves(u32 *dst, u32 w, u32 h)
{
    static const u32 lc[7] = { 0x7FD6E8, 0x45B6D8, 0x2A93C4, 0x1D74B0, 0x14568F, 0x0D3C70, 0x082752 };
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            i32 X = vx[x], Y = vy[y];
            u32 c = vgrad(RGB(255, 205, 150), RGB(255, 245, 225), Y, 0, 260);
            c = addc(c, RGB(255, 200, 120), glow(X, Y, 760, 250, 200) * 100 >> 8);
            for (int k = 0; k < 7; k++) {
                i32 crest = 250 + k * 70 + (isin(X / 3 + k * 53) * (20 + k * 4) >> 8) + (isin(X / 7 + k * 31) * 12 >> 8);
                if (Y >= crest) {
                    u32 depth = (u32)(Y - crest);
                    c = lc[k];
                    c = mixc(c, RGB(235, 250, 255), depth < 10 ? (10 - depth) * 20 : 0);
                }
            }
            dst[y * w + x] = c;
        }
}

static void sc_nebula(u32 *dst, u32 w, u32 h)
{
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            i32 X = vx[x], Y = vy[y];
            u32 n1 = fbm2(X * 2, Y * 2, 31), n2 = fbm2(X * 2 + 9000, Y * 2 + 3000, 41);
            u32 c = RGB(6, 4, 22);
            u32 a = n1 > 110 ? (n1 - 110) * 2 : 0;
            if (a > 256) a = 256;
            c = addc(c, RGB(150, 40, 170), a * 3 / 4);
            u32 b = n2 > 120 ? (n2 - 120) * 2 : 0;
            if (b > 256) b = 256;
            c = addc(c, RGB(30, 120, 220), b * 3 / 4);
            u32 core = glow(X, Y, 560, 380, 300);
            c = addc(c, RGB(255, 120, 190), core * (n1 + 40) >> 11);
            u32 st = star(X, Y, 7, 7);
            c = addc(c, RGB(255, 255, 255), st);
            dst[y * w + x] = c;
        }
}

static void sc_dunes(u32 *dst, u32 w, u32 h)
{
    static const u32 lc[4] = { 0xF0B97A, 0xD8935A, 0xB46E45, 0x7F4A3C };
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            i32 X = vx[x], Y = vy[y];
            u32 c = vgrad(RGB(255, 150, 90), RGB(255, 225, 170), Y, 0, 400);
            c = addc(c, RGB(255, 235, 200), glow(X, Y, 300, 330, 150) * 200 >> 8);
            for (int k = 0; k < 4; k++) {
                i32 p = X / 3 + k * 70;
                i32 crest = 380 + k * 95 + (isin(p) * (50 - k * 6) >> 8) + (isin(X / 8 + k * 40) * 14 >> 8);
                if (Y >= crest) {
                    c = lc[k];
                    i32 slope = isin(p + 64);                /* правая сторона гребня в тени */
                    if (slope < 0) c = mixc(c, RGB(60, 30, 40), (u32)(-slope) * 90 >> 8);
                    else c = addc(c, RGB(255, 200, 140), (u32)slope * 40 >> 8);
                }
            }
            dst[y * w + x] = c;
        }
}

static void sc_borealis(u32 *dst, u32 w, u32 h)
{
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            i32 X = vx[x], Y = vy[y];
            u32 c = vgrad(RGB(2, 6, 22), RGB(10, 30, 50), Y, 0, 600);
            c = addc(c, RGB(255, 255, 255), star(X, Y, 3, 4));
            for (int k = 0; k < 2; k++) {
                i32 yc = 250 + k * 60 + (isin(X / 4 + k * 70) >> 2) + (isin(X / 11 + k * 20) >> 3);
                i32 d = Y - yc;
                u32 ad = (u32)(d < 0 ? -d : d);
                u32 band = d < 0 ? (ad < 150 ? (150 - ad) * (150 - ad) / 90 : 0) : (ad < 40 ? (40 - ad) * 4 : 0);
                u32 ray = 120 + vn1(X * 2 + k * 5000, 5 + (u32)k) * 135 / 255;
                u32 a = band * ray >> 8;
                if (a > 256) a = 256;
                c = addc(c, k ? RGB(60, 255, 190) : RGB(90, 255, 120), a * 3 / 4);
                c = addc(c, RGB(160, 60, 220), d > 40 && d < 110 ? a / 6 : 0);
            }
            i32 hh = 640 + (isin(X / 3) >> 3) + (isin(X / 7 + 50) >> 4);
            if (Y >= hh) c = RGB(6, 12, 20);
            dst[y * w + x] = c;
        }
}

static void sc_city(u32 *dst, u32 w, u32 h)
{
    for (int layer = 0; layer < 2; layer++) {
        u16 *hh = layer ? bh1 : bh0, *id = layer ? bid1 : bid0;
        u32 minw = layer ? 40 : 28, span = layer ? 50 : 40;
        u32 x = 0, n = 0;
        while (x < 1024) {
            u32 r = hs(n, 100 + (u32)layer);
            u32 bw = minw + r % span, bhgt = (layer ? 150 : 200) + ((r >> 8) % (layer ? 220 : 240));
            for (u32 i = 0; i < bw && x + i < 1024; i++) {
                hh[x + i] = (u16)bhgt;
                id[x + i] = (u16)(n * 64 + i / 8);
            }
            x += bw;
            n++;
        }
    }
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            i32 X = vx[x], Y = vy[y];
            u32 c = vgrad(RGB(14, 12, 48), RGB(210, 90, 130), Y, 0, 640);
            c = addc(c, RGB(255, 255, 255), Y < 300 ? star(X, Y, 13, 3) : 0);
            i32 top0 = 700 - bh0[X];
            if (Y >= top0) {
                c = RGB(44, 30, 84);
                if (((hs((u32)bid0[X], (u32)(Y - top0) / 12) & 7) == 0) && (X % 8) > 2 && ((Y - top0) % 12) > 4)
                    c = RGB(150, 120, 150);
            }
            i32 top1 = 780 - bh1[X];
            if (Y >= top1) {
                c = RGB(12, 8, 30);
                u32 r = hs((u32)bid1[X], (u32)(Y - top1) / 14 + 7);
                if (((r & 3) == 0) && (X % 8) > 2 && ((Y - top1) % 14) > 5)
                    c = (r >> 4 & 3) ? RGB(255, 214, 120) : RGB(130, 220, 255);
            }
            dst[y * w + x] = c;
        }
}

static void sc_bokeh(u32 *dst, u32 w, u32 h)
{
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            i32 X = vx[x], Y = vy[y];
            u32 c = mixc(vgrad(RGB(40, 20, 110), RGB(220, 70, 150), Y, 0, 768), RGB(60, 90, 220), (u32)X * 90 >> 10);
            for (u32 k = 0; k < 22; k++) {
                u32 r = hs(k, 77);
                i32 cx = (i32)(r & 1023), cy = (i32)((r >> 10) & 1023) * 768 / 1024;
                i32 rad = 40 + (i32)((r >> 20) & 63) * 2;
                u32 g = glow(X, Y, cx, cy, rad);
                if (!g) continue;
                static const u32 pc[4] = { 0xFFB6E0, 0x9FD0FF, 0xFFE6A0, 0xC9A6FF };
                c = addc(c, pc[k & 3], g * (30 + (r >> 26) * 2) >> 8);
            }
            dst[y * w + x] = c;
        }
}

static void sc_forest(u32 *dst, u32 w, u32 h)
{
    static const u32 lc[5] = { 0x93B7B0, 0x6E998F, 0x4B7A70, 0x2F5A52, 0x173A38 };
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            i32 X = vx[x], Y = vy[y];
            u32 c = vgrad(RGB(190, 220, 215), RGB(240, 245, 235), Y, 0, 420);
            c = addc(c, RGB(255, 250, 220), glow(X, Y, 760, 190, 170) * 190 >> 8);
            for (int k = 0; k < 5; k++) {
                i32 period = 70 - k * 8;
                i32 ph = (X + k * 37) % period;
                i32 tri = ph < period / 2 ? ph : period - ph;           /* ёлочки: зубцы */
                i32 base = 330 + k * 85 - (i32)(vn1(X * 2 + k * 3000, 50 + (u32)k) * 70 >> 8);
                i32 top = base - tri * (3 + k) + (isin(X / 6 + k * 9) >> 5);
                if (Y >= top) {
                    c = lc[k];
                    if (k < 4) c = mixc(c, RGB(235, 245, 240), (u32)(Y - top) < 90 ? 90 - (u32)(Y - top) : 0);
                }
            }
            dst[y * w + x] = c;
        }
}

typedef struct { const char *name; void (*fn)(u32 *, u32, u32); } scene_t;
static const scene_t scenes[WALL_COUNT - GRAD_COUNT] = {
    { "Mountains", sc_mountains }, { "Starry Night", sc_starry }, { "Neon Grid", sc_neon },
    { "Waves", sc_waves },         { "Nebula", sc_nebula },       { "Dunes", sc_dunes },
    { "Northern Lights", sc_borealis }, { "City Night", sc_city }, { "Bokeh", sc_bokeh },
    { "Misty Forest", sc_forest },
};

const char *wallpaper_name(int id)
{
    return id < GRAD_COUNT ? wps[id].name : scenes[id - GRAD_COUNT].name;
}

void wallpaper_paint(int id, u32 *dst, u32 w, u32 h)
{
    if (id < GRAD_COUNT) {
        paint_gradient(id, dst, w, h);
        return;
    }
    for (u32 x = 0; x < w; x++) vx[x] = (u16)(x * 1024 / w);
    for (u32 y = 0; y < h; y++) vy[y] = (u16)(y * 768 / h);
    scenes[id - GRAD_COUNT].fn(dst, w, h);
}
