/* Плавные переходы оболочки: приближение значений и смешивание кадров. */
#include "anim.h"
#include "fb.h"

#define OV_TMP_W 512
#define OV_TMP_H 512

static u32 ov_bg[FB_MAX_W * FB_MAX_H];          /* фон под панелью или весь прежний кадр */
static u32 ov_tmp[OV_TMP_W * OV_TMP_H];         /* нарисованная панель перед сдвигом */
static i32 rx, ry, rw, rh;

i32 anim_approach(i32 v, i32 target, u32 ticks)
{
    if (ticks > 12) ticks = 12;
    while (ticks-- && v != target) {
        i32 d = target - v;
        i32 step = d * 28 / 100;
        if (step == 0) step = d > 0 ? 1 : -1;
        v += step;
    }
    return v;
}

i32 anim_ease_out(i32 p)
{
    if (p <= 0) return 0;
    if (p >= 1024) return 1024;
    i32 q = 1024 - p;
    q = (q * q) >> 10;
    q = (q * (1024 - p)) >> 10;
    return 1024 - q;
}

static inline u32 mix(u32 dst, u32 src, u32 a)  /* a = 0..256 */
{
    u32 ia = 256 - a;
    u32 rb = (((dst & 0xFF00FF) * ia + (src & 0xFF00FF) * a) >> 8) & 0xFF00FF;
    u32 g  = (((dst & 0x00FF00) * ia + (src & 0x00FF00) * a) >> 8) & 0x00FF00;
    return rb | g;
}

static u32 alpha256(i32 a1024)
{
    if (a1024 <= 0) return 0;
    if (a1024 >= 1024) return 256;
    return (u32)a1024 >> 2;
}

void anim_region_begin(i32 x, i32 y, i32 w, i32 h)
{
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    i32 x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    rx = x; ry = y; rw = x1 - x; rh = y1 - y;
    if (rw <= 0 || rh <= 0 || rw > OV_TMP_W || rh > OV_TMP_H) { rw = rh = 0; return; }
    const u32 *fbuf = fb_buffer();
    for (i32 j = 0; j < rh; j++)
        for (i32 i = 0; i < rw; i++)
            ov_bg[j * rw + i] = fbuf[(ry + j) * W + rx + i];
}

void anim_region_end(i32 dy, i32 alpha, i32 clip_top)
{
    if (!rw || !rh) return;
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    u32 *fbuf = fb_buffer();
    u32 a = alpha256(alpha);
    for (i32 j = 0; j < rh; j++)
        for (i32 i = 0; i < rw; i++)
            ov_tmp[j * rw + i] = fbuf[(ry + j) * W + rx + i];
    for (i32 j = 0; j < rh; j++)                    /* вернуть фон */
        for (i32 i = 0; i < rw; i++)
            fbuf[(ry + j) * W + rx + i] = ov_bg[j * rw + i];
    for (i32 j = 0; j < rh; j++) {
        i32 y = ry + j + dy;
        if (y < clip_top || y < 0 || y >= H) continue;
        u32 *row = &fbuf[y * W + rx];
        for (i32 i = 0; i < rw; i++) {
            u32 t = ov_tmp[j * rw + i];
            if (t == ov_bg[j * rw + i]) continue;   /* здесь панель ничего не изменила */
            row[i] = mix(row[i], t, a);
        }
    }
}

void anim_screen_save(void)
{
    i32 n = (i32)(fb_width() * fb_height());
    const u32 *fbuf = fb_buffer();
    for (i32 i = 0; i < n; i++)
        ov_bg[i] = fbuf[i];
}

void anim_screen_blend(i32 alpha)
{
    i32 n = (i32)(fb_width() * fb_height());
    u32 *fbuf = fb_buffer();
    u32 a = alpha256(alpha);
    for (i32 i = 0; i < n; i++)
        fbuf[i] = mix(ov_bg[i], fbuf[i], a);
}
