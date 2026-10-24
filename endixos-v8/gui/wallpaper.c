/* Процедурные обои: трёхцветный градиент вдоль заданного направления + мягкое свечение. */
#include "wallpaper.h"
#include "fb.h"

typedef struct {
    const char *name;
    u8 wx, wy;               /* вклад осей в градиент, wx + wy <= 255 */
    u8 c0[3], c1[3], c2[3];  /* три опорных цвета градиента */
    u8 gx, gy, gs;           /* центр свечения (в 1/256 экрана) и его сила */
} wp_t;

static const wp_t wps[WALL_COUNT] = {
    { "Aurora",   128, 127, {40, 30, 120},  {95, 55, 165},  {150, 80, 210},  210,  40,  70 },
    { "Ocean",     60, 195, {8, 30, 80},    {14, 100, 150}, {60, 200, 190},   60,  40,  60 },
    { "Sunset",    20, 235, {50, 25, 100},  {220, 90, 110}, {255, 175, 90},  128, 215, 120 },
    { "Forest",   140, 115, {10, 45, 40},   {30, 110, 70},  {150, 200, 90},  210,  50,  60 },
    { "Graphite", 100, 155, {18, 18, 22},   {45, 46, 54},   {90, 92, 104},   128, 100,  45 },
    { "Dawn",     120, 135, {255, 214, 194}, {245, 160, 170}, {170, 130, 210}, 60,  40,  90 },
};

#define GLOW_LIM 32768          /* квадрат радиуса свечения в единицах 1/256 экрана */

static u32 pal[256];
static u16 tx[FB_MAX_W], ty[FB_MAX_H];
static u32 dx2[FB_MAX_W], dy2[FB_MAX_H];

const char *wallpaper_name(int id)
{
    return wps[id].name;
}

void wallpaper_paint(int id, u32 *dst, u32 w, u32 h)
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
