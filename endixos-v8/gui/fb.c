/* Графика поверх линейного фреймбуфера (32 bpp) с двойной буферизацией. */
#include "fb.h"
#include "../kernel/string.h"

static u8  *screen;                                /* видеопамять */
static u32  s_pitch, s_width, s_height;
static u32  backbuf[FB_MAX_W * FB_MAX_H];          /* теневой буфер (bss) */
static u32  bgbuf[FB_MAX_W * FB_MAX_H];            /* кэш обоев */

int fb_init(u32 addr, u32 pitch, u32 width, u32 height, u32 bpp)
{
    if (bpp != 32 || width > FB_MAX_W || height > FB_MAX_H)
        return -1;
    screen   = (u8 *)addr;
    s_pitch  = pitch;
    s_width  = width;
    s_height = height;
    return 0;
}

u32 fb_width(void)  { return s_width; }
u32 fb_height(void) { return s_height; }

static inline u32 mix(u32 dst, u32 src, u32 alpha)
{
    u32 inv = 255 - alpha;
    u32 r = (((src >> 16) & 0xFF) * alpha + ((dst >> 16) & 0xFF) * inv + 127) / 255;
    u32 g = (((src >> 8)  & 0xFF) * alpha + ((dst >> 8)  & 0xFF) * inv + 127) / 255;
    u32 b = ((src         & 0xFF) * alpha + (dst         & 0xFF) * inv + 127) / 255;
    return (r << 16) | (g << 8) | b;
}

void fb_put_pixel(i32 x, i32 y, u32 color)
{
    if (x < 0 || y < 0 || (u32)x >= s_width || (u32)y >= s_height)
        return;
    backbuf[(u32)y * s_width + (u32)x] = color;
}

void fb_blend_pixel(i32 x, i32 y, u32 color, u32 alpha)
{
    if (x < 0 || y < 0 || (u32)x >= s_width || (u32)y >= s_height)
        return;
    u32 *p = &backbuf[(u32)y * s_width + (u32)x];
    if (alpha >= 255)
        *p = color;
    else if (alpha)
        *p = mix(*p, color, alpha);
}

/* Горизонтальный отрезок [x0, x1) на строке y с отсечением по экрану. */
static void span(i32 y, i32 x0, i32 x1, u32 color, u32 alpha)
{
    if (y < 0 || (u32)y >= s_height)
        return;
    if (x0 < 0) x0 = 0;
    if (x1 > (i32)s_width) x1 = (i32)s_width;
    if (x0 >= x1 || alpha == 0)
        return;

    u32 *p = &backbuf[(u32)y * s_width + (u32)x0];
    i32 n = x1 - x0;
    if (alpha >= 255) {
        for (i32 i = 0; i < n; i++)
            p[i] = color;
    } else {
        for (i32 i = 0; i < n; i++)
            p[i] = mix(p[i], color, alpha);
    }
}

void fb_fill_rect(i32 x, i32 y, i32 w, i32 h, u32 color)
{
    for (i32 j = 0; j < h; j++)
        span(y + j, x, x + w, color, 255);
}

void fb_blend_rect(i32 x, i32 y, i32 w, i32 h, u32 color, u32 alpha)
{
    for (i32 j = 0; j < h; j++)
        span(y + j, x, x + w, color, alpha);
}

void fb_fill_round_rect(i32 x, i32 y, i32 w, i32 h, i32 r, u32 color, u32 alpha)
{
    if (w <= 0 || h <= 0)
        return;
    if (r > 32) r = 32;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    /* inset[k] - на сколько строка k от края сдвинута внутрь из-за скругления */
    i32 inset[32];
    for (i32 k = 0; k < r; k++) {
        i32 dy = r - 1 - k;
        i32 i = 0;
        while (i < r && (r - 1 - i) * (r - 1 - i) + dy * dy >= r * r)
            i++;
        inset[k] = i;
    }

    for (i32 j = 0; j < h; j++) {
        i32 in = 0;
        if (j < r)           in = inset[j];
        else if (j >= h - r) in = inset[h - 1 - j];
        span(y + j, x + in, x + w - in, color, alpha);
    }
}

void fb_fill_circle(i32 cx, i32 cy, i32 r, u32 color)
{
    for (i32 j = -r; j <= r; j++) {
        i32 dx = 0;
        while ((dx + 1) * (dx + 1) + j * j <= r * r)
            dx++;
        span(cy + j, cx - dx, cx + dx + 1, color, 255);
    }
}

u32 *fb_buffer(void)
{
    return backbuf;
}

void fb_blit(i32 x, i32 y, const u32 *src, i32 w, i32 h)
{
    for (i32 j = 0; j < h; j++) {
        i32 yy = y + j;
        if (yy < 0 || (u32)yy >= s_height)
            continue;
        i32 x0 = x < 0 ? 0 : x;
        i32 x1 = x + w > (i32)s_width ? (i32)s_width : x + w;
        if (x0 >= x1)
            continue;
        memcpy(&backbuf[(u32)yy * s_width + (u32)x0], &src[j * w + (x0 - x)], (u32)(x1 - x0) * 4);
    }
}

void fb_save_background(void)
{
    memcpy(bgbuf, backbuf, s_width * s_height * 4);
}

void fb_restore_background(void)
{
    memcpy(backbuf, bgbuf, s_width * s_height * 4);
}

void fb_flip(void)
{
    for (u32 y = 0; y < s_height; y++)
        memcpy(screen + y * s_pitch, &backbuf[y * s_width], s_width * 4);
}
