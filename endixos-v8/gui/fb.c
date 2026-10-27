/* Графика поверх линейного фреймбуфера (32 bpp) с двойной буферизацией. */
#include "fb.h"
#include "../kernel/string.h"

static u8  *screen;                                /* видеопамять */
static u32  s_pitch, s_width, s_height;
static u32  backbuf[FB_MAX_W * FB_MAX_H];          /* теневой буфер (bss) */
static u32  flipped_bytes;                         /* счётчик записей в видеопамять (для замеров) */
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

/* Смешивание двух цветов сразу по парам каналов (R+B и G): без делений и без цикла по каналам.
 * alpha 0..255; вес a = alpha + alpha/128 даёт ровно 0 и 256 на краях. */
static inline u32 mix(u32 dst, u32 src, u32 alpha)
{
    u32 a = alpha + (alpha >> 7), ia = 256 - a;
    u32 rb = (((dst & 0xFF00FF) * ia + (src & 0xFF00FF) * a) >> 8) & 0xFF00FF;
    u32 g  = (((dst & 0x00FF00) * ia + (src & 0x00FF00) * a) >> 8) & 0x00FF00;
    return rb | g;
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

/* Курсор живёт только в видеопамяти: сцена (backbuf) остаётся чистой. */
void fb_frame_put_pixel(i32 x, i32 y, u32 color)
{
    if (x < 0 || y < 0 || (u32)x >= s_width || (u32)y >= s_height)
        return;
    *(u32 *)(screen + (u32)y * s_pitch + (u32)x * 4) = color;
}

/* Вернуть на экран чистую сцену под прямоугольником (стереть курсор). */
void fb_frame_restore(i32 x, i32 y, i32 w, i32 h)
{
    i32 x0 = x < 0 ? 0 : x, x1 = x + w > (i32)s_width ? (i32)s_width : x + w;
    if (x0 >= x1)
        return;
    for (i32 j = y; j < y + h; j++) {
        if (j < 0 || (u32)j >= s_height)
            continue;
        memcpy(screen + (u32)j * s_pitch + (u32)x0 * 4, &backbuf[(u32)j * s_width + (u32)x0],
               (u32)(x1 - x0) * 4);
        flipped_bytes += (u32)(x1 - x0) * 4;
    }
}

/* Сцена целиком на экран одним проходом. */
void fb_flip(void)
{
    for (u32 y = 0; y < s_height; y++)
        memcpy(screen + y * s_pitch, &backbuf[y * s_width], s_width * 4);
    flipped_bytes += s_width * s_height * 4;
}

u32 fb_flipped_bytes(void)
{
    return flipped_bytes;
}

/* ===================== Liquid glass ===================== */
/* Размытие фона за панелью (бегущая сумма, два прохода), тонировка, световая кромка и блик. */

#define GLASS_TMP (FB_MAX_W * 340)
static u32 gtmp[GLASS_TMP];                         /* результат горизонтального прохода */
static u32 gcs_rb[FB_MAX_W], gcs_g[FB_MAX_W];

static void glass_insets(i32 r, i32 *inset)         /* inset[k]: сдвиг строки k от края из-за скругления */
{
    for (i32 k = 0; k < r; k++) {
        i32 dy = r - 1 - k, i = 0;
        while (i < r && (r - 1 - i) * (r - 1 - i) + dy * dy >= r * r)
            i++;
        inset[k] = i;
    }
}

static inline void rim(u32 *row, i32 x0, i32 x1, u32 a)     /* белая подсветка отрезка */
{
    if (!a) return;
    if (a > 255) a = 255;
    for (i32 x = x0; x < x1; x++)
        row[x] = mix(row[x], 0xFFFFFF, a);
}


/* Лёгкое повышение насыщенности размытого фона - "живое" стекло. sat: 0..256 */
static inline u32 vivid(u32 c, i32 sat)
{
    i32 r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    i32 l = (r * 77 + g * 150 + b * 29) >> 8;
    r += ((r - l) * sat) >> 8; g += ((g - l) * sat) >> 8; b += ((b - l) * sat) >> 8;
    r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (u32)(r << 16 | g << 8 | b);
}

static int glass_reduced;
static int gp[GLASS_PARAMS] = { 60, 50, 50, 60, 25 };      /* преломление, размытие, матовость, кромка, насыщенность (0..100) */
static const int gp_def[GLASS_PARAMS] = { 60, 50, 50, 60, 25 };

void fb_glass_param_set(int id, int v) { if (id >= 0 && id < GLASS_PARAMS) gp[id] = v < 0 ? 0 : v > 100 ? 100 : v; }
int  fb_glass_param_get(int id) { return id >= 0 && id < GLASS_PARAMS ? gp[id] : 0; }
void fb_glass_param_reset(void) { for (int i = 0; i < GLASS_PARAMS; i++) gp[i] = gp_def[i]; }

void fb_glass_set_reduced(int on) { glass_reduced = on; }

void fb_glass(i32 x, i32 y, i32 w, i32 h, i32 r, u32 corners, i32 br, u32 tint, u32 ta, u32 sheen)
{
    if (glass_reduced) {                 /* "Reduce transparency": без размытия, плотнее тон */
        br = 0;
        ta = ta + 70 > 255 ? 255 : ta + 70;
    }
    if (w <= 0 || h <= 0)
        return;
    int lq = (corners & 15) == 15 && h >= 20 && w >= 40 && br > 0 && !glass_reduced;   /* парящая панель: "жидкое стекло" */
    if (lq) {
        br = br * gp[GLASS_BLUR] / 50;
        if (br > 20) br = 20;
        ta = ta * (u32)gp[GLASS_FROST] / 50;
        if (ta > 255) ta = 255;
    }
    if (r > 32) r = 32;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    i32 W = (i32)s_width, H = (i32)s_height;
    i32 inset[32];
    glass_insets(r, inset);

    i32 y0 = y < 0 ? 0 : y, y1 = y + h > H ? H : y + h;
    i32 x0 = x < 0 ? 0 : x, x1 = x + w > W ? W : x + w;
    if (x0 >= x1 || y0 >= y1)
        return;
    if (lq)                                              /* мягкая тень снизу */
        fb_shadow(x, y, w, h, h / 12 + 2, h / 4 < 14 ? h / 4 + 3 : 18, 34);

    /* вставки левого и правого краёв для строки j панели */
    #define LI(j) ((j) < r ? ((corners & 1) ? inset[j] : 0) : ((j) >= h - r ? ((corners & 4) ? inset[h - 1 - (j)] : 0) : 0))
    #define RI(j) ((j) < r ? ((corners & 2) ? inset[j] : 0) : ((j) >= h - r ? ((corners & 8) ? inset[h - 1 - (j)] : 0) : 0))

    if (br > 0) {
        /* окно размытия - степень двойки: деление на неё - просто сдвиг, а R и B считаются вместе */
        int wb = br <= 5 ? 3 : (br >= 14 ? 5 : 4);
        i32 lo = (1 << wb) / 2, hi = (1 << wb) - lo - 1;
        i32 ex0 = x0 - lo - 1 < 0 ? 0 : x0 - lo - 1, ex1 = x1 + hi + 1 > W ? W : x1 + hi + 1;
        i32 ey0 = y0 - lo - 1 < 0 ? 0 : y0 - lo - 1, ey1 = y1 + hi + 1 > H ? H : y1 + hi + 1;
        i32 rw = ex1 - ex0, rh = ey1 - ey0;
        if (rw * rh <= GLASS_TMP) {
            /* горизонтальный проход: backbuf -> gtmp */
            for (i32 yy = 0; yy < rh; yy++) {
                const u32 *src = &backbuf[(u32)(ey0 + yy) * s_width + (u32)ex0];
                u32 *dst = &gtmp[yy * rw];
                u32 srb = 0, sg = 0;
                for (i32 i = -lo; i <= hi; i++) {
                    u32 c = src[i < 0 ? 0 : (i >= rw ? rw - 1 : i)];
                    srb += c & 0xFF00FF; sg += (c >> 8) & 255;
                }
                for (i32 xx = 0; xx < rw; xx++) {
                    dst[xx] = ((srb >> wb) & 0xFF00FF) | (((sg >> wb) & 255) << 8);
                    u32 a_ = src[xx + hi + 1 >= rw ? rw - 1 : xx + hi + 1];
                    u32 b_ = src[xx - lo < 0 ? 0 : xx - lo];
                    srb += a_ & 0xFF00FF; srb -= b_ & 0xFF00FF;
                    sg += ((a_ >> 8) & 255) - ((b_ >> 8) & 255);
                }
            }
            /* вертикальный проход по строкам: суммы столбцов хранятся в gcs_* */
            i32 cx0 = x0 - ex0, cw = x1 - x0;
            i32 ty = y0 - ey0;
            for (i32 c = 0; c < cw; c++) { gcs_rb[c] = 0; gcs_g[c] = 0; }
            for (i32 i = -lo; i <= hi; i++) {
                i32 t = ty + i < 0 ? 0 : (ty + i >= rh ? rh - 1 : ty + i);
                const u32 *row = &gtmp[t * rw + cx0];
                for (i32 c = 0; c < cw; c++) {
                    gcs_rb[c] += row[c] & 0xFF00FF; gcs_g[c] += (row[c] >> 8) & 255;
                }
            }
            for (i32 yy = y0; yy < y1; yy++) {
                i32 j = yy - y;
                i32 xs = x + LI(j), xe = x + w - RI(j);
                if (xs < x0) xs = x0;
                if (xe > x1) xe = x1;
                u32 *dst = &backbuf[(u32)yy * s_width];
                for (i32 xx = xs; xx < xe; xx++) {
                    i32 c = xx - x0;
                    dst[xx] = ((gcs_rb[c] >> wb) & 0xFF00FF) | (((gcs_g[c] >> wb) & 255) << 8);
                }
                i32 ta_ = ty + (yy - y0) + hi + 1, tb = ty + (yy - y0) - lo;
                const u32 *ra = &gtmp[(ta_ >= rh ? rh - 1 : ta_) * rw + cx0];
                const u32 *rb = &gtmp[(tb < 0 ? 0 : tb) * rw + cx0];
                for (i32 c = 0; c < cw; c++) {
                    gcs_rb[c] += ra[c] & 0xFF00FF; gcs_rb[c] -= rb[c] & 0xFF00FF;
                    gcs_g[c] += ((ra[c] >> 8) & 255) - ((rb[c] >> 8) & 255);
                }
            }
        }
    }

    /* "Жидкое стекло": у краёв панели фон будто преломляется - берётся из глубины панели, чем ближе к краю, тем сильнее. */
    int liquid = lq && gp[GLASS_REFRACT] > 0;
    if (liquid) {
        i32 pw = x1 - x0, ph = y1 - y0;
        i32 B = h / 3 < 12 ? h / 3 : 12;
        if (B > 4 && pw * ph <= GLASS_TMP) {
            for (i32 yy = 0; yy < ph; yy++)
                for (i32 xx = 0; xx < pw; xx++)
                    gtmp[yy * pw + xx] = backbuf[(u32)(y0 + yy) * s_width + (u32)(x0 + xx)];
            for (i32 yy = y0; yy < y1; yy++) {
                i32 j = yy - y;
                i32 xs = x + LI(j), xe = x + w - RI(j);
                if (xs < x0) xs = x0;
                if (xe > x1) xe = x1;
                i32 dyt = yy - y, dyb = y + h - 1 - yy;
                i32 my = dyt < dyb ? dyt : dyb;
                u32 *row = &backbuf[(u32)yy * s_width];
                for (i32 xx = xs; xx < xe; xx++) {
                    i32 dxl = xx - x, dxr = x + w - 1 - xx;
                    i32 mx = dxl < dxr ? dxl : dxr;
                    if (mx >= B && my >= B) { xx = xe - B > xx ? xe - B - 1 : xx; continue; }
                    i32 sx = xx, sy = yy;
                    if (mx < B) { i32 q = B - mx; i32 sh = B * q * q * gp[GLASS_REFRACT] / (B * B * 100); sx += dxl < dxr ? sh : -sh; }
                    if (my < B) { i32 q = B - my; i32 sh = B * q * q * gp[GLASS_REFRACT] / (B * B * 100); sy += dyt < dyb ? sh : -sh; }
                    if (sx < x0) sx = x0;
                    if (sx >= x1) sx = x1 - 1;
                    if (sy < y0) sy = y0;
                    if (sy >= y1) sy = y1 - 1;
                    u32 c = gtmp[(sy - y0) * pw + (sx - x0)];
                    i32 e = (mx < my ? mx : my);
                    row[xx] = mix(c, 0xFFFFFF, (u32)((B - e) * 26 / B));     /* у самой кромки чуть светлее */
                }
            }
        }
    }

    /* тонировка, блик сверху и световая кромка */
    u32 rimk = (corners & 16) ? 0 : 100;              /* бит 16: без световой кромки; 100 = обычная яркость */
    if (lq) { rimk = rimk * (u32)gp[GLASS_EDGE] / 60; sheen = sheen * (u32)gp[GLASS_EDGE] / 60; }
    i32 sh_h = h * 45 / 100 > 1 ? h * 45 / 100 : 1;
    for (i32 yy = y0; yy < y1; yy++) {
        i32 j = yy - y;
        i32 li = LI(j), ri = RI(j);
        i32 xs = x + li, xe = x + w - ri;
        i32 cs = xs < x0 ? x0 : xs, ce = xe > x1 ? x1 : xe;
        u32 *row = &backbuf[(u32)yy * s_width];
        if (lq && gp[GLASS_VIVID])
            for (i32 xx = cs; xx < ce; xx++)
                row[xx] = vivid(row[xx], gp[GLASS_VIVID] * 256 / 100);
        if (ta)
            for (i32 xx = cs; xx < ce; xx++)
                row[xx] = mix(row[xx], tint, ta);
        if (lq) {                                    /* внутренняя игра света: светлее сверху, темнее у нижней кромки */
            if (j == 2) rim(row, cs, ce, 22);
            else if (j == 3) rim(row, cs, ce, 10);
            else if (j == h - 2) for (i32 xx = cs; xx < ce; xx++) row[xx] = mix(row[xx], 0x000000, 26);
            else if (j == h - 3) for (i32 xx = cs; xx < ce; xx++) row[xx] = mix(row[xx], 0x000000, 12);
        }
        if (sheen && j < sh_h) {
            u32 a = sheen * (u32)((sh_h - j) * (sh_h - j)) / (u32)(sh_h * sh_h);
            if (a) rim(row, cs, ce, a);
        }
        /* верхняя кромка: пиксели строки, которых нет строкой выше */
        i32 pl = j == 0 ? xe : x + LI(j - 1), pr = j == 0 ? xs : x + w - RI(j - 1);
        if (pl > xe) pl = xe;
        if (pr < xs) pr = xs;
        i32 a0 = xs, a1 = pl, b0 = pr, b1 = xe;
        if (j == 0) { a0 = xs; a1 = xe; b0 = b1 = xe; }
        if (a0 < x0) a0 = x0;
        if (a1 > x1) a1 = x1;
        if (a1 > a0) rim(row, a0, a1, 175 * rimk / 100);
        if (b0 < x0) b0 = x0;
        if (b1 > x1) b1 = x1;
        if (b1 > b0) rim(row, b0, b1, 175 * rimk / 100);
        /* нижняя кромка */
        if (j + 1 >= h) {
            rim(row, cs, ce, 60 * rimk / 100);
        } else {
            i32 nl = x + LI(j + 1), nr = x + w - RI(j + 1);
            i32 c0 = xs, c1 = nl < xe ? nl : xe, d0 = nr > xs ? nr : xs, d1 = xe;
            if (c0 < x0) c0 = x0;
            if (c1 > x1) c1 = x1;
            if (c1 > c0) rim(row, c0, c1, 60 * rimk / 100);
            if (d0 < x0) d0 = x0;
            if (d1 > x1) d1 = x1;
            if (d1 > d0) rim(row, d0, d1, 60 * rimk / 100);
        }
        /* боковые кромки: левая светлее правой */
        if (xs >= x0 && xs < x1) rim(row, xs, xs + 1, 120 * rimk / 100);
        if (xe - 1 >= x0 && xe - 1 < x1 && xe - 1 > xs) rim(row, xe - 1, xe, 70 * rimk / 100);
        /* внутреннее мягкое свечение у верхней кромки */
        if (j == 1) rim(row, cs, ce, 38 * rimk / 100);
    }
    #undef LI
    #undef RI
}

/* Мягкая тень окна: за один проход по строкам, без вызова на каждый пиксель.
 * Область - прямоугольник (x,y,w,h), сдвинутый вниз на dy; spread - на сколько пикселей тень выходит за него. */
void fb_shadow(i32 x, i32 y, i32 w, i32 h, i32 dy, i32 spread, u32 amax)
{
    i32 W = (i32)s_width, H = (i32)s_height;
    i32 sy = y + dy, in = 7;
    u32 al[64];                                    /* alpha для расстояния k = -in .. spread */
    i32 n = spread + in + 1;
    if (n > 64) return;
    for (i32 i = 0; i < n; i++) {
        i32 f = n - 1 - i;
        al[i] = amax * (u32)(f * f) / (u32)((n - 1) * (n - 1));
    }
    for (i32 yy = sy - spread; yy < sy + h + spread; yy++) {
        if (yy < 0 || yy >= H) continue;
        i32 dyo = yy < sy ? sy - yy : (yy >= sy + h ? yy - (sy + h - 1) : -((yy - sy + 1) < (sy + h - yy) ? (yy - sy + 1) : (sy + h - yy)) + 1);
        u32 *row = &backbuf[(u32)yy * s_width];
        i32 xa = x - spread, xb = x + w + spread;
        i32 skip0 = 0, skip1 = 0;                  /* середина строки пропускается, если внутри окна */
        if (dyo < -in) { skip0 = x + in; skip1 = x + w - in; }
        for (i32 xx = xa < 0 ? 0 : xa; xx < xb && xx < W; xx++) {
            if (xx >= skip0 && xx < skip1) { xx = skip1 - 1; continue; }
            i32 dxo = x - xx > xx - (x + w - 1) ? x - xx : xx - (x + w - 1);
            i32 k = dxo > dyo ? dxo : dyo;
            if (k < -in || k > spread) continue;
            u32 a = al[k + in];
            if (!a) continue;
            u32 ia = 256 - (a + (a >> 7)), p = row[xx];
            row[xx] = ((((p & 0xFF00FF) * ia) >> 8) & 0xFF00FF) | ((((p & 0x00FF00) * ia) >> 8) & 0x00FF00);
        }
    }
}
