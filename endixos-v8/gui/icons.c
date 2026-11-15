/* Значки приложений в стиле macOS: скруглённая "суперэллиптическая" плитка, вертикальные градиенты,
 * блик сверху, тонкая светлая кромка и тёмная внизу. Фигуры задаются знаковыми полями расстояний
 * (в 1/16 пикселя), покрытие края считается аналитически - линии получаются ровными. */
#include "icons.h"
#include "fb.h"
#include "theme.h"

#define S ICON_S
#define U 16                         /* единиц на пиксель */

enum { T_RR, T_CI, T_CA };

typedef struct {
    int t;
    i32 a, b, c, d, e;               /* RR: cx cy hw hh r | CI: cx cy r | CA: x1 y1 x2 y2 r (в пикселях) */
    int rot;                         /* поворот в шагах по 22.5 градуса */
    i32 rx, ry;                      /* центр поворота */
    i32 stroke;                      /* >0: только внутренняя кромка такой толщины (1/16 px) */
} shp;

static i32 g_er;                          /* 1: фигура стирает альфу (вырез) */
static i32 g_style, g_tone;                /* стиль значков; g_tone: 2/3 - цвета фигур переводятся в одноцветные */
static u32 g_accent;
static i32 g_sh;                          /* >0: рисуем тень фигуры, смещённую вниз на g_sh px */

typedef struct { u32 c0, c1; i32 a0, a1; i32 y0, y1; } paint;

static const i32 COS16[16] = { 256, 237, 181, 98, 0, -98, -181, -237, -256, -237, -181, -98, 0, 98, 181, 237 };
static const i32 SIN16[16] = { 0, 98, 181, 237, 256, 237, 181, 98, 0, -98, -181, -237, -256, -237, -181, -98 };

static u32 isqrt(u32 v)
{
    u32 r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

static i32 iabs(i32 v) { return v < 0 ? -v : v; }
static i32 imax(i32 a, i32 b) { return a > b ? a : b; }
static i32 imin(i32 a, i32 b) { return a < b ? a : b; }

static i32 hyp(i32 x, i32 y)
{
    return (i32)isqrt((u32)(x * x + y * y));
}

static i32 sd_raw(const shp *s, i32 px, i32 py)
{
    if (s->rot) {
        i32 dx = px - s->rx * U, dy = py - s->ry * U;
        i32 c = COS16[s->rot & 15], sn = SIN16[s->rot & 15];
        px = s->rx * U + ((dx * c + dy * sn) >> 8);
        py = s->ry * U + ((-dx * sn + dy * c) >> 8);
    }
    switch (s->t) {
    case T_RR: {
        i32 qx = iabs(px - s->a * U) - s->c * U + s->e * U;
        i32 qy = iabs(py - s->b * U) - s->d * U + s->e * U;
        i32 ox = imax(qx, 0), oy = imax(qy, 0);
        i32 out = (ox > 0 && oy > 0) ? hyp(ox, oy) : imax(ox, oy);
        return out + imin(imax(qx, qy), 0) - s->e * U;
    }
    case T_CI:
        return hyp(px - s->a * U, py - s->b * U) - s->c * U;
    default: {
        i32 x1 = s->a * U, y1 = s->b * U, x2 = s->c * U, y2 = s->d * U;
        i32 vx = x2 - x1, vy = y2 - y1, wx = px - x1, wy = py - y1;
        i32 l2 = vx * vx + vy * vy;
        i32 t = 0;
        if (l2 > 0) {
            i32 dot = (vx * wx + vy * wy);
            t = ((dot >> 4) * 256) / ((l2 >> 4) + 1);          /* без 64-бит: в ядре нет libgcc */
            if (t < 0) t = 0;
            if (t > 256) t = 256;
        }
        return hyp(wx - ((vx * t) >> 8), wy - ((vy * t) >> 8)) - s->e * U;
    }
    }
}

static i32 sd(const shp *s, i32 px, i32 py)
{
    i32 d = sd_raw(s, px, py);
    if (s->stroke > 0)
        d = imax(d, -d - s->stroke);
    return d;
}

static i32 cover(i32 d)                  /* 0..255 */
{
    i32 c = U / 2 - d;
    if (c <= 0) return 0;
    if (c >= U) return 255;
    return c * 255 / U;
}

static u32 lerp_c(u32 a, u32 b, i32 t, i32 n)      /* t/n */
{
    if (n <= 0) return a;
    if (t < 0) t = 0;
    if (t > n) t = n;
    u32 r = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        i32 x = (i32)(a >> sh & 255), y = (i32)(b >> sh & 255);
        r |= (u32)(x + (y - x) * t / n) << sh;
    }
    return r;
}

static void bbox(const shp *s, i32 *x0, i32 *y0, i32 *x1, i32 *y1)
{
    i32 pad = 3 + (g_sh > 0 ? g_sh : 0);
    if (s->rot || s->stroke < 0) { *x0 = 0; *y0 = 0; *x1 = S; *y1 = S; }
    else if (s->t == T_RR) { *x0 = s->a - s->c - pad; *x1 = s->a + s->c + pad; *y0 = s->b - s->d - pad; *y1 = s->b + s->d + pad; }
    else if (s->t == T_CI) { *x0 = s->a - s->c - pad; *x1 = s->a + s->c + pad; *y0 = s->b - s->c - pad; *y1 = s->b + s->c + pad; }
    else {
        *x0 = imin(s->a, s->c) - s->e - pad; *x1 = imax(s->a, s->c) + s->e + pad;
        *y0 = imin(s->b, s->d) - s->e - pad; *y1 = imax(s->b, s->d) + s->e + pad;
    }
    if (s->rot) {                         /* повёрнутая фигура: берём круг вокруг центра поворота */
        i32 R = 0;
        if (s->t == T_RR) R = hyp(s->c, s->d);
        else if (s->t == T_CI) R = s->c + iabs(s->a - s->rx) + iabs(s->b - s->ry);
        else R = S;
        R += pad + 2 + (s->t == T_RR ? iabs(s->a - s->rx) + iabs(s->b - s->ry) : 0);
        *x0 = s->rx - R; *x1 = s->rx + R; *y0 = s->ry - R; *y1 = s->ry + R;
    }
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > S) *x1 = S;
    if (*y1 > S) *y1 = S;
}

static void put(u32 *cv, i32 x, i32 y, u32 col, i32 a)
{
    if (a <= 0) return;
    u32 d = cv[y * S + x];
    i32 da = (i32)(d >> 24);
    if (a >= 255 || da == 0) {
        cv[y * S + x] = (u32)a << 24 | (col & 0xFFFFFF);
        return;
    }
    i32 oa = a + da * (255 - a) / 255;
    u32 r = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        i32 sc = (i32)(col >> sh & 255), dc = (i32)(d >> sh & 255);
        i32 v = (sc * a * 255 + dc * da * (255 - a)) / (oa * 255);
        if (v > 255) v = 255;
        r |= (u32)v << sh;
    }
    cv[y * S + x] = (u32)oa << 24 | r;
}

static u32 tone_map(u32 c)
{
    u32 r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
    u32 lum = (30 * r + 59 * g + 11 * b) / 100, v = 110 + lum * 145 / 255;       /* глифы всегда светлые на тёмной плитке */
    if (g_tone == 2) return v << 16 | v << 8 | v;
    u32 tr = (g_accent >> 16 & 255) + (255 - (g_accent >> 16 & 255)) * 45 / 100;
    u32 tg = (g_accent >> 8 & 255) + (255 - (g_accent >> 8 & 255)) * 45 / 100;
    u32 tb = (g_accent & 255) + (255 - (g_accent & 255)) * 45 / 100;
    return (tr * v / 255) << 16 | (tg * v / 255) << 8 | (tb * v / 255);
}

static void fill(u32 *cv, const shp *s, const shp *clip, paint p)
{
    if (g_tone && !g_er) { p.c0 = tone_map(p.c0); p.c1 = tone_map(p.c1); }
    i32 x0, y0, x1, y1;
    bbox(s, &x0, &y0, &x1, &y1);
    for (i32 y = y0; y < y1; y++) {
        u32 col = lerp_c(p.c0, p.c1, y - p.y0, p.y1 - p.y0);
        i32 al = p.a0 + (p.a1 - p.a0) * imax(0, imin(y - p.y0, p.y1 - p.y0)) / imax(1, p.y1 - p.y0);
        for (i32 x = x0; x < x1; x++) {
            i32 px = x * U + U / 2, py = y * U + U / 2 - g_sh * U;
            i32 c = cover(sd(s, px, py));
            if (!c) continue;
            if (clip) {
                i32 k = cover(sd(clip, px, py));
                if (!k) continue;
                c = c * k / 255;
            }
            if (g_er) { u32 d = cv[y * S + x]; i32 na = (i32)(d >> 24) * (255 - c) / 255; cv[y * S + x] = (u32)na << 24 | (d & 0xFFFFFF); }
            else if (g_sh) put(cv, x, y, 0x000000, c * al / 255 * 30 / 255);
            else put(cv, x, y, col, c * al / 255);
        }
    }
}

/* ---- сокращения ---- */
static shp RR(i32 cx, i32 cy, i32 hw, i32 hh, i32 r) { shp s = { T_RR, cx, cy, hw, hh, r, 0, 0, 0, 0 }; return s; }
static shp CI(i32 cx, i32 cy, i32 r)                  { shp s = { T_CI, cx, cy, r, 0, 0, 0, 0, 0, 0 }; return s; }
static shp CA(i32 x1, i32 y1, i32 x2, i32 y2, i32 r)  { shp s = { T_CA, x1, y1, x2, y2, r, 0, 0, 0, 0 }; return s; }
static shp ROT(shp s, int k, i32 rx, i32 ry)          { s.rot = k; s.rx = rx; s.ry = ry; return s; }
static shp STK(shp s, i32 w)                          { s.stroke = w; return s; }

static paint P(u32 c0, u32 c1, i32 a, i32 y0, i32 y1) { paint p = { c0, c1, a, a, y0, y1 }; return p; }
static paint PA(u32 c, i32 a0, i32 a1, i32 y0, i32 y1) { paint p = { c, c, a0, a1, y0, y1 }; return p; }
static paint SOLID(u32 c)                             { return P(c, c, 255, 0, S); }

#define C(r, g, b) ((u32)(r) << 16 | (u32)(g) << 8 | (u32)(b))
#define WHITE C(255, 255, 255)
#define BLACK C(0, 0, 0)

static shp tile(void) { return RR(64, 64, 62, 62, 29); }

static void base(u32 *cv, u32 top, u32 bot)
{
    shp t = tile();
    i32 tone = g_tone; g_tone = 0;                              /* плитка красится своими цветами */
    if (g_style == ICON_STYLE_MONO) { top = C(70, 70, 76); bot = C(14, 14, 18); }
    else if (g_style == ICON_STYLE_TINT) { top = lerp_c(g_accent, BLACK, 60, 100); bot = lerp_c(g_accent, BLACK, 88, 100); }
    if (g_style == ICON_STYLE_GLASS) {                          /* Liquid Glass: полупрозрачная плитка, яркая кромка и блик */
        fill(cv, &t, 0, P(lerp_c(top, WHITE, 18, 100), lerp_c(bot, WHITE, 8, 100), 165, 2, 126));
        fill(cv, &t, 0, PA(WHITE, 120, 0, 2, 80));
        shp inner = STK(tile(), 5 * U);
        fill(cv, &inner, 0, PA(WHITE, 0, 70, 60, 126));         /* свет снизу изнутри */
        shp rim = STK(tile(), 2 * U + U / 2);
        fill(cv, &rim, 0, PA(WHITE, 235, 90, 2, 126));
    } else {
        fill(cv, &t, 0, P(top, bot, 255, 2, 126));
        fill(cv, &t, 0, PA(WHITE, g_style ? 46 : 70, 0, 2, 72));        /* блик сверху */
        shp rim = STK(tile(), U + U / 2);
        fill(cv, &rim, 0, PA(WHITE, g_style ? 90 : 130, 0, 2, 56));     /* светлая кромка */
        fill(cv, &rim, 0, PA(BLACK, 0, 70, 70, 126));                   /* тёмная внизу */
    }
    g_tone = tone;
}

static void mix_white(u32 c, i32 pct, u32 *out)
{
    *out = lerp_c(c, WHITE, pct, 100);
}

/* Тень глифа: три прохода с разным смещением дают мягкий край. */
#define SHADOWED(...) do { static const i32 off_[3] = { 5, 3, 2 }; \
    for (int sp_ = 0; sp_ < 3; sp_++) { g_sh = off_[sp_]; __VA_ARGS__ } g_sh = 0; __VA_ARGS__ } while (0)

/* ---- Launchpad: серебристая плитка с цветной сеткой ---- */
static void ic_launchpad(u32 *cv)
{
    base(cv, C(238, 241, 247), C(168, 176, 196));
    static const u32 hi[9] = { C(110,180,255), C(255,190,80), C(110,222,120), C(255,110,100), C(170,140,255), C(255,226,90), C(100,222,235), C(255,130,190), C(170,176,190) };
    static const u32 lo[9] = { C(30,110,240), C(250,130,10), C(30,170,70), C(235,50,50), C(100,70,230), C(250,180,10), C(30,170,200), C(230,60,140), C(110,116,132) };
    for (int k = 0; k < 9; k++) {
        i32 cx = 33 + (k % 3) * 31, cy = 33 + (k / 3) * 31;
        shp t = RR(cx, cy, 13, 13, 6);
        fill(cv, &t, 0, P(hi[k], lo[k], 255, cy - 13, cy + 13));
        shp g = STK(RR(cx, cy, 13, 13, 6), U);
        fill(cv, &g, 0, PA(WHITE, 80, 0, cy - 13, cy + 6));
    }
}

/* ---- Browix: глобус ---- */
static void ic_browser(u32 *cv)
{
    base(cv, C(96, 224, 214), C(24, 96, 220));
    shp disc = CI(64, 64, 46);
    fill(cv, &disc, 0, P(C(60, 150, 240), C(20, 70, 190), 255, 18, 110));
    shp ring = STK(CI(64, 64, 46), 5 * U);
    fill(cv, &ring, 0, P(C(255, 255, 255), C(206, 230, 255), 255, 18, 110));
    shp mer = STK(RR(64, 64, 20, 46, 20), 3 * U);
    fill(cv, &mer, 0, PA(WHITE, 235, 235, 0, S));
    shp clip = CI(64, 64, 45);
    shp v = CA(64, 20, 64, 108, 1), h0 = CA(18, 64, 110, 64, 1), h1 = CA(24, 41, 104, 41, 1), h2 = CA(24, 87, 104, 87, 1);
    fill(cv, &v, &clip, PA(WHITE, 235, 235, 0, S)); fill(cv, &h0, &clip, PA(WHITE, 235, 235, 0, S));
    fill(cv, &h1, &clip, PA(WHITE, 190, 190, 0, S)); fill(cv, &h2, &clip, PA(WHITE, 190, 190, 0, S));
    shp sp = CI(48, 44, 7);                                     /* блик на «стекле» */
    fill(cv, &sp, &clip, PA(WHITE, 90, 0, 30, 58));
}

/* ---- Files: папка ---- */
static void ic_files(u32 *cv)
{
    base(cv, C(40, 200, 255), C(0, 120, 250));
    SHADOWED({
        shp tab = RR(46, 40, 19, 9, 5), back = RR(64, 58, 40, 24, 7);
        fill(cv, &tab, 0, P(C(176, 214, 255), C(130, 182, 250), 255, 31, 49));
        fill(cv, &back, 0, P(C(176, 214, 255), C(128, 180, 250), 255, 34, 82));
    });
    shp front = RR(64, 74, 42, 25, 8);
    fill(cv, &front, 0, P(C(255, 255, 255), C(206, 226, 255), 255, 49, 99));
    shp fh = STK(RR(64, 74, 42, 25, 8), U);
    fill(cv, &fh, 0, PA(WHITE, 200, 0, 49, 70));
}

/* ---- Terminal ---- */
static void ic_terminal(u32 *cv)
{
    base(cv, C(76, 78, 88), C(18, 18, 22));
    shp br = STK(tile(), 2 * U);
    fill(cv, &br, 0, PA(WHITE, 40, 10, 2, 126));
    SHADOWED({
        shp a = CA(34, 48, 54, 64, 4), b = CA(54, 64, 34, 80, 4), c = CA(64, 82, 90, 82, 4);
        paint p = P(WHITE, C(206, 212, 224), 255, 44, 86);
        fill(cv, &a, 0, p); fill(cv, &b, 0, p); fill(cv, &c, 0, p);
    });
}

/* ---- Settings: шестерёнка ---- */
static void ic_settings(u32 *cv)
{
    base(cv, C(214, 216, 224), C(126, 130, 142));
    SHADOWED({
        for (int k = 0; k < 8; k += 2) {
            shp tooth = ROT(RR(64, 64, 8, 45, 3), k, 64, 64);
            fill(cv, &tooth, 0, P(C(116, 120, 132), C(58, 62, 74), 255, 19, 109));
        }
        shp body = CI(64, 64, 35);
        fill(cv, &body, 0, P(C(122, 126, 138), C(60, 64, 76), 255, 29, 99));
    });
    shp hi = STK(CI(64, 64, 35), 24);
    fill(cv, &hi, 0, PA(WHITE, 100, 0, 29, 90));
    shp hole = CI(64, 64, 15);
    fill(cv, &hole, 0, P(C(140, 144, 156), C(214, 216, 224), 255, 49, 79));
    shp hr = STK(CI(64, 64, 15), 24);
    fill(cv, &hr, 0, PA(BLACK, 110, 20, 49, 79));
}

/* ---- Snake: змейка из блоков, как в самой игре ---- */
static void ic_snake(u32 *cv)
{
    base(cv, C(110, 224, 110), C(24, 146, 66));
    static const i32 sx[9] = { 26, 46, 66, 86, 86, 86, 66, 46, 46 };
    static const i32 sy[9] = { 94, 94, 94, 94, 74, 54, 54, 54, 34 };
    SHADOWED({
        for (int i = 0; i < 9; i++) {
            i32 hw = i == 8 ? 11 : 9;
            shp b = RR(sx[i], sy[i], hw, hw, 6);
            u32 c0 = i == 8 ? WHITE : C(246, 255, 244), c1 = i == 8 ? C(222, 244, 214) : C(204, 240, 190);
            if (i < 3) { c0 = lerp_c(c0, C(160, 220, 150), 3 - i, 5); c1 = lerp_c(c1, C(110, 190, 110), 3 - i, 5); }
            fill(cv, &b, 0, P(c0, c1, 255, sy[i] - hw, sy[i] + hw));
        }
        shp ap = CI(88, 30, 10);
        fill(cv, &ap, 0, P(C(255, 120, 104), C(214, 36, 44), 255, 20, 40));
    });
    shp e1 = CI(41, 33, 2), e2 = CI(51, 33, 2);
    fill(cv, &e1, 0, SOLID(C(20, 70, 36)));
    fill(cv, &e2, 0, SOLID(C(20, 70, 36)));
    shp lf = CA(88, 21, 94, 15, 2);
    fill(cv, &lf, 0, SOLID(C(30, 120, 56)));
    shp hl = CI(84, 26, 2);
    fill(cv, &hl, 0, PA(WHITE, 170, 170, 0, S));
}

/* ---- About: белый клевер EndixOS на плитке цвета акцента ---- */
static void ic_about(u32 *cv, u32 accent, int dark)
{
    u32 lt, dk;
    mix_white(accent, dark ? 14 : 30, &lt);
    dk = lerp_c(accent, BLACK, dark ? 55 : 28, 100);
    base(cv, lt, dk);
    SHADOWED({
        paint p = P(WHITE, C(228, 232, 244), 255, 22, 106);
        shp c1 = CI(43, 43, 21), c2 = CI(85, 43, 21), c3 = CI(43, 85, 21), c4 = CI(85, 85, 21);
        shp h = RR(64, 64, 21, 36, 0), v = RR(64, 64, 36, 21, 0);
        fill(cv, &c1, 0, p); fill(cv, &c2, 0, p); fill(cv, &c3, 0, p); fill(cv, &c4, 0, p);
        fill(cv, &h, 0, p); fill(cv, &v, 0, p);
    });
}

/* ---- Глифы боковых панелей: рисуются цветом col, вырезы стирают альфу ---- */
static void cut(u32 *cv, const shp *s, const shp *clip) { g_er = 1; fill(cv, s, clip, SOLID(WHITE)); g_er = 0; }

static void glyph(u32 *cv, int kind, u32 col)
{
    paint p = SOLID(col);
    switch (kind) {
    case 0: {                                           /* дом */
        shp roof = ROT(RR(64, 56, 30, 30, 6), 2, 64, 56), top = RR(64, 24, 80, 40, 0);
        fill(cv, &roof, &top, p);
        shp body = RR(64, 82, 30, 22, 5);
        fill(cv, &body, 0, p);
        shp door = RR(64, 92, 7, 12, 3);
        cut(cv, &door, 0);
        break;
    }
    case 1: {                                           /* монитор */
        shp scr = RR(64, 54, 46, 34, 8), neck = RR(64, 94, 6, 8, 0), base = RR(64, 104, 20, 5, 3);
        fill(cv, &scr, 0, p);
        fill(cv, &neck, 0, p);
        fill(cv, &base, 0, p);
        shp in = RR(64, 54, 39, 27, 3);
        cut(cv, &in, 0);
        break;
    }
    case 2: {                                           /* документ */
        shp pg = RR(64, 64, 34, 48, 8);
        fill(cv, &pg, 0, p);
        shp corner = ROT(RR(98, 22, 14, 14, 0), 2, 98, 22);
        cut(cv, &corner, 0);
        shp l1 = CA(46, 62, 82, 62, 3), l2 = CA(46, 78, 82, 78, 3), l3 = CA(46, 94, 66, 94, 3);
        cut(cv, &l1, 0); cut(cv, &l2, 0); cut(cv, &l3, 0);
        break;
    }
    case 3: {                                           /* загрузки */
        shp c = CI(64, 64, 50);
        fill(cv, &c, 0, p);
        shp st = CA(64, 36, 64, 86, 6), a1 = CA(44, 68, 64, 88, 6), a2 = CA(84, 68, 64, 88, 6);
        cut(cv, &st, 0); cut(cv, &a1, 0); cut(cv, &a2, 0);
        break;
    }
    case 4: {                                           /* фото */
        shp r = RR(64, 64, 52, 42, 10);
        fill(cv, &r, 0, p);
        shp sun = CI(44, 44, 9);
        cut(cv, &sun, 0);
        shp m1 = ROT(RR(60, 104, 26, 26, 4), 2, 60, 104), m2 = ROT(RR(92, 106, 19, 19, 4), 2, 92, 106);
        cut(cv, &m1, &r); cut(cv, &m2, &r);
        break;
    }
    case 5: {                                           /* нота */
        shp h = CI(44, 92, 16), st = CA(58, 90, 58, 30, 5), fl = CA(58, 32, 96, 46, 6), fl2 = CA(96, 46, 96, 66, 5);
        fill(cv, &h, 0, p); fill(cv, &st, 0, p); fill(cv, &fl, 0, p); fill(cv, &fl2, 0, p);
        break;
    }
    case 6: {                                           /* диск */
        shp d = RR(64, 76, 56, 28, 12);
        fill(cv, &d, 0, p);
        shp led = CI(92, 80, 6), ln = CA(34, 80, 62, 80, 4);
        cut(cv, &led, 0); cut(cv, &ln, 0);
        shp top = CA(30, 50, 98, 50, 4);
        fill(cv, &top, 0, P(col, col, 140, 0, S));
        break;
    }
    case 8: {                                           /* стекло: скруглённая линза с бликом */
        shp ring = STK(RR(64, 64, 46, 46, 20), 9 * U);
        fill(cv, &ring, 0, p);
        shp dot = CI(64, 64, 15);
        fill(cv, &dot, 0, P(col, col, 200, 0, S));
        shp hl = CA(40, 48, 48, 40, 4);
        fill(cv, &hl, 0, p);
        break;
    }
    case 7: {                                           /* оформление: круг, половина залита */
        shp ring = STK(CI(64, 64, 48), 9 * U);
        fill(cv, &ring, 0, p);
        shp half = CI(64, 64, 40), rh = RR(96, 64, 32, 64, 0);
        fill(cv, &half, &rh, p);
        break;
    }
    }
}


/* ---- эмодзи для аватарок: рисуются фигурами, без плитки ---- */
static const u32 DK = 0x5A3A22;
static void eyes(u32 *cv, i32 y, i32 dx, i32 r) { shp a = CI(64 - dx, y, r), b = CI(64 + dx, y, r); fill(cv, &a, 0, SOLID(DK)); fill(cv, &b, 0, SOLID(DK)); }
static void smile(u32 *cv, i32 y, i32 w, i32 d)          /* улыбка из отрезков по дуге */
{
    i32 xs[5] = { 64 - w, 64 - w * 6 / 10, 64, 64 + w * 6 / 10, 64 + w };
    i32 ys[5] = { y, y + d * 7 / 10, y + d, y + d * 7 / 10, y };
    for (int i = 0; i < 4; i++) { shp l = CA(xs[i], ys[i], xs[i + 1], ys[i + 1], 3); fill(cv, &l, 0, SOLID(DK)); }
}
static void yface(u32 *cv)
{
    shp f = CI(64, 64, 54);
    SHADOWED({ fill(cv, &f, 0, P(C(255, 226, 86), C(255, 178, 36), 255, 10, 118)); });
    shp h = STK(CI(64, 64, 54), U);
    fill(cv, &h, 0, PA(WHITE, 150, 0, 10, 60));
}
static void ic_emoji(u32 *cv, int n)
{
    switch (n) {
    case 0: yface(cv); eyes(cv, 54, 20, 6); smile(cv, 78, 26, 14); {
        shp c1 = CI(34, 76, 7), c2 = CI(94, 76, 7); fill(cv, &c1, 0, PA(C(255, 120, 110), 120, 120, 0, S)); fill(cv, &c2, 0, PA(C(255, 120, 110), 120, 120, 0, S)); } break;
    case 1: yface(cv); {                                                       /* очки */
        shp a = RR(44, 56, 17, 11, 6), b = RR(84, 56, 17, 11, 6), br = CA(58, 54, 70, 54, 2);
        fill(cv, &a, 0, P(C(60, 62, 72), C(14, 14, 18), 255, 45, 67)); fill(cv, &b, 0, P(C(60, 62, 72), C(14, 14, 18), 255, 45, 67)); fill(cv, &br, 0, SOLID(C(14, 14, 18)));
        shp g1 = CA(36, 52, 46, 50, 2); fill(cv, &g1, 0, PA(WHITE, 160, 160, 0, S)); }
        smile(cv, 82, 24, 12); break;
    case 2: yface(cv); { shp e = CI(64 + 20, 54, 6); fill(cv, &e, 0, SOLID(DK));
        shp l1 = CA(34, 56, 44, 50, 3), l2 = CA(44, 50, 54, 56, 3); fill(cv, &l1, 0, SOLID(DK)); fill(cv, &l2, 0, SOLID(DK)); }
        smile(cv, 78, 26, 14); break;
    case 3: {                                                                  /* сердце */
        shp c1 = CI(46, 48, 26), c2 = CI(82, 48, 26), d = ROT(RR(64, 72, 30, 30, 5), 2, 64, 72);
        SHADOWED({ paint p = P(C(255, 110, 120), C(222, 24, 56), 255, 20, 116); fill(cv, &c1, 0, p); fill(cv, &c2, 0, p); fill(cv, &d, 0, p); });
        shp hl = CA(36, 40, 42, 33, 4); fill(cv, &hl, 0, PA(WHITE, 190, 190, 0, S)); break; }
    case 4: {                                                                  /* звезда */
        shp a = ROT(RR(64, 64, 32, 32, 5), 0, 64, 64), b = ROT(RR(64, 64, 32, 32, 5), 2, 64, 64);
        shp c = ROT(RR(64, 64, 8, 52, 3), 0, 64, 64), d = ROT(RR(64, 64, 52, 8, 3), 0, 64, 64);
        SHADOWED({ paint p = P(C(255, 236, 110), C(255, 176, 20), 255, 12, 116); fill(cv, &b, 0, p); fill(cv, &a, 0, p); fill(cv, &c, 0, p); fill(cv, &d, 0, p); });
        break; }
    case 5: {                                                                  /* кот */
        shp e1 = ROT(RR(34, 36, 17, 17, 4), 2, 34, 36), e2 = ROT(RR(94, 36, 17, 17, 4), 2, 94, 36), h = CI(64, 70, 46);
        SHADOWED({ paint p = P(C(255, 200, 124), C(242, 150, 70), 255, 24, 116); fill(cv, &e1, 0, p); fill(cv, &e2, 0, p); fill(cv, &h, 0, p); });
        shp i1 = ROT(RR(34, 38, 9, 9, 2), 2, 34, 38), i2 = ROT(RR(94, 38, 9, 9, 2), 2, 94, 38); fill(cv, &i1, 0, SOLID(C(255, 160, 170))); fill(cv, &i2, 0, SOLID(C(255, 160, 170)));
        eyes(cv, 64, 20, 6); shp n = CI(64, 76, 5); fill(cv, &n, 0, SOLID(C(240, 110, 130)));
        shp w1 = CA(14, 78, 38, 80, 1), w2 = CA(14, 90, 38, 86, 1), w3 = CA(114, 78, 90, 80, 1), w4 = CA(114, 90, 90, 86, 1);
        fill(cv, &w1, 0, SOLID(DK)); fill(cv, &w2, 0, SOLID(DK)); fill(cv, &w3, 0, SOLID(DK)); fill(cv, &w4, 0, SOLID(DK));
        smile(cv, 88, 10, 6); break; }
    case 6: {                                                                  /* собака */
        shp e1 = RR(24, 62, 13, 28, 10), e2 = RR(104, 62, 13, 28, 10), h = CI(64, 66, 42), sn = CI(64, 84, 22);
        SHADOWED({ fill(cv, &e1, 0, SOLID(C(150, 100, 60))); fill(cv, &e2, 0, SOLID(C(150, 100, 60))); fill(cv, &h, 0, P(C(236, 190, 130), C(212, 160, 100), 255, 24, 108)); });
        fill(cv, &sn, 0, SOLID(C(250, 228, 196))); eyes(cv, 58, 18, 6);
        shp n = CI(64, 76, 8); fill(cv, &n, 0, SOLID(C(40, 30, 30)));
        shp m = CA(64, 84, 64, 92, 2); fill(cv, &m, 0, SOLID(DK)); break; }
    case 7: {                                                                  /* панда */
        shp e1 = CI(30, 30, 17), e2 = CI(98, 30, 17), h = CI(64, 68, 46);
        SHADOWED({ fill(cv, &e1, 0, SOLID(C(40, 40, 46))); fill(cv, &e2, 0, SOLID(C(40, 40, 46))); fill(cv, &h, 0, P(WHITE, C(228, 230, 236), 255, 22, 114)); });
        shp p1 = ROT(RR(46, 64, 11, 15, 8), 1, 46, 64), p2 = ROT(RR(82, 64, 11, 15, 8), 15, 82, 64);
        fill(cv, &p1, 0, SOLID(C(40, 40, 46))); fill(cv, &p2, 0, SOLID(C(40, 40, 46)));
        shp w1 = CI(47, 63, 4), w2 = CI(81, 63, 4), n = CI(64, 80, 6);
        fill(cv, &w1, 0, SOLID(WHITE)); fill(cv, &w2, 0, SOLID(WHITE)); fill(cv, &n, 0, SOLID(C(40, 40, 46))); smile(cv, 88, 10, 6); break; }
    case 8: {                                                                  /* робот */
        shp ant = CA(64, 36, 64, 22, 3), tip = CI(64, 18, 7), hd = RR(64, 72, 42, 36, 12);
        fill(cv, &ant, 0, SOLID(C(150, 156, 170))); fill(cv, &tip, 0, SOLID(C(255, 70, 70)));
        SHADOWED({ fill(cv, &hd, 0, P(C(222, 226, 236), C(150, 158, 176), 255, 36, 108)); });
        shp e1 = CI(46, 68, 10), e2 = CI(82, 68, 10), m = RR(64, 94, 24, 6, 3);
        fill(cv, &e1, 0, P(C(120, 240, 255), C(20, 160, 220), 255, 58, 78)); fill(cv, &e2, 0, P(C(120, 240, 255), C(20, 160, 220), 255, 58, 78));
        fill(cv, &m, 0, SOLID(C(60, 66, 80))); break; }
    case 9: {                                                                  /* привидение */
        shp top = CI(64, 56, 40), body = RR(64, 84, 40, 30, 0);
        SHADOWED({ paint p = P(WHITE, C(214, 222, 240), 255, 16, 116); fill(cv, &top, 0, p); fill(cv, &body, 0, p); });
        for (int k = 0; k < 4; k++) { shp w = CI(32 + k * 21, 118, 11); cut(cv, &w, 0); }
        shp e1 = CI(48, 56, 7), e2 = CI(80, 56, 7), m = CI(64, 76, 6);
        fill(cv, &e1, 0, SOLID(C(50, 56, 80))); fill(cv, &e2, 0, SOLID(C(50, 56, 80))); fill(cv, &m, 0, SOLID(C(50, 56, 80))); break; }
    case 10: {                                                                 /* инопланетянин */
        shp h = CI(64, 60, 46), ch = RR(64, 86, 22, 24, 12);
        SHADOWED({ paint p = P(C(150, 240, 120), C(70, 190, 80), 255, 14, 116); fill(cv, &h, 0, p); fill(cv, &ch, 0, p); });
        shp e1 = ROT(RR(44, 62, 11, 17, 9), 2, 44, 62), e2 = ROT(RR(84, 62, 11, 17, 9), 14, 84, 62);
        fill(cv, &e1, 0, SOLID(C(20, 40, 30))); fill(cv, &e2, 0, SOLID(C(20, 40, 30)));
        shp g1 = CI(40, 56, 3), g2 = CI(88, 56, 3); fill(cv, &g1, 0, SOLID(WHITE)); fill(cv, &g2, 0, SOLID(WHITE));
        shp m = CA(56, 96, 72, 96, 2); fill(cv, &m, 0, SOLID(C(30, 90, 40))); break; }
    case 11: {                                                                 /* огонь */
        shp b = CI(64, 82, 36), t = ROT(RR(64, 44, 22, 22, 4), 2, 64, 44);
        SHADOWED({ paint p = P(C(255, 190, 50), C(244, 70, 30), 255, 14, 118); fill(cv, &t, 0, p); fill(cv, &b, 0, p); });
        shp ib = CI(64, 94, 20), it = ROT(RR(64, 72, 12, 12, 3), 2, 64, 72);
        paint q = P(C(255, 250, 160), C(255, 190, 50), 255, 62, 114); fill(cv, &it, 0, q); fill(cv, &ib, 0, q); break; }
    case 12: {                                                                 /* луна */
        shp m = CI(60, 64, 48), c = CI(86, 50, 40);
        SHADOWED({ fill(cv, &m, 0, P(C(255, 236, 130), C(246, 190, 60), 255, 16, 112)); });
        cut(cv, &c, 0); break; }
    case 13: {                                                                 /* солнце */
        shp c = CI(64, 64, 28);
        for (int k = 0; k < 16; k += 2) { shp r = ROT(RR(64, 20, 5, 12, 3), k, 64, 64); fill(cv, &r, 0, SOLID(C(255, 190, 40))); }
        SHADOWED({ fill(cv, &c, 0, P(C(255, 236, 110), C(255, 176, 30), 255, 36, 92)); }); break; }
    case 14: {                                                                 /* цветок */
        static const i32 px[5] = { 64, 91, 81, 47, 37 }, py[5] = { 34, 54, 86, 86, 54 };
        SHADOWED({ for (int k = 0; k < 5; k++) { shp pt = CI(px[k], py[k], 21); fill(cv, &pt, 0, P(C(255, 170, 200), C(244, 100, 150), 255, py[k] - 21, py[k] + 21)); } });
        shp c = CI(64, 64, 15); fill(cv, &c, 0, P(C(255, 236, 110), C(250, 180, 30), 255, 49, 79)); break; }
    }
}

static void tile_icon(u32 *cv, int kind)
{
    if (kind == 0) {                                    /* обои: голубая плитка с горами */
        base(cv, C(110, 200, 255), C(30, 120, 245));
        SHADOWED({ glyph(cv, 4, WHITE); });
    } else if (kind == 2) {                             /* Liquid Glass: фиолетовая плитка */
        base(cv, C(176, 150, 255), C(88, 70, 226));
        SHADOWED({ glyph(cv, 8, WHITE); });
    } else {                                            /* оформление: серая плитка */
        base(cv, C(122, 126, 140), C(58, 60, 72));
        SHADOWED({ glyph(cv, 7, WHITE); });
    }
}

/* ---- значки на рабочем столе: папка, документ, корзина (один материал: градиент, бумага, мягкая тень; тёмная тема - своя палитра) ---- */
static void ic_desk_folder(u32 *cv, int dark)
{
    u32 f0 = dark ? C(10, 160, 240) : C(0, 184, 255), f1 = dark ? C(10, 98, 216) : C(0, 128, 255);
    u32 b0 = dark ? C(10, 140, 216) : C(0, 170, 245), b1 = dark ? C(10, 90, 190) : C(0, 120, 235);
    u32 p0 = dark ? C(208, 208, 216) : C(244, 244, 246), p1 = dark ? C(142, 142, 152) : C(200, 200, 204);
    SHADOWED({
        shp back = RR(36, 42, 26, 20, 7), front = RR(64, 75, 54, 36, 7);
        fill(cv, &back, 0, P(b0, b1, 255, 22, 62));
        fill(cv, &front, 0, P(f0, f1, 255, 39, 111));
    });
    shp paper = RR(63, 47, 48, 15, 7);
    fill(cv, &paper, 0, P(p0, p1, 255, 33, 62));
    shp front = RR(64, 75, 54, 36, 7);
    fill(cv, &front, 0, P(f0, f1, 255, 39, 111));
    shp fh = STK(RR(64, 75, 54, 36, 7), U);
    fill(cv, &fh, 0, PA(WHITE, dark ? 90 : 140, 0, 39, 66));
}

static void ic_desk_file(u32 *cv, u32 accent, int dark)
{
    u32 g0 = dark ? C(88, 92, 104) : C(255, 255, 255), g1 = dark ? C(58, 61, 72) : C(226, 231, 242);
    u32 e0 = dark ? C(120, 124, 138) : C(206, 212, 226), e1 = dark ? C(84, 88, 100) : C(160, 168, 188);
    u32 ln = dark ? C(132, 137, 152) : (accent & 0xFFFFFF);
    SHADOWED({
        shp pg = RR(64, 64, 40, 54, 8);
        fill(cv, &pg, 0, P(g0, g1, 255, 10, 118));
    });
    shp edge = STK(RR(64, 64, 40, 54, 8), U);
    fill(cv, &edge, 0, P(e0, e1, 255, 10, 118));
    shp cut1 = ROT(RR(104, 10, 17, 17, 0), 2, 104, 10);
    cut(cv, &cut1, 0);
    shp fold = ROT(RR(88, 26, 12, 12, 3), 2, 88, 26), clip = RR(64, 64, 40, 54, 8);
    fill(cv, &fold, &clip, P(dark ? C(120, 124, 138) : C(238, 242, 250), dark ? C(84, 88, 100) : C(188, 196, 214), 255, 14, 40));
    static const i32 ly[4] = { 58, 72, 86, 100 };
    for (int i = 0; i < 4; i++) {
        shp l = CA(40, ly[i], i == 3 ? 66 : 88, ly[i], 3);
        fill(cv, &l, 0, P(ln, ln, dark ? 255 : 150, 0, S));
    }
}

static void ic_trash(u32 *cv, int full, int dark)
{
    u32 b0 = dark ? C(112, 118, 140) : C(222, 229, 241), b1 = dark ? C(70, 75, 94) : C(148, 160, 184);
    u32 l0 = dark ? C(134, 140, 162) : C(244, 247, 252), l1 = dark ? C(92, 98, 120) : C(176, 187, 206);
    u32 sl = dark ? C(40, 43, 56) : C(128, 140, 164);
    if (full) {
        shp p1 = ROT(RR(52, 28, 19, 15, 3), 2, 52, 28), p2 = ROT(RR(76, 26, 19, 17, 3), 1, 76, 26);
        fill(cv, &p1, 0, P(dark ? C(200, 204, 214) : WHITE, dark ? C(150, 154, 168) : C(214, 220, 232), 255, 16, 40));
        fill(cv, &p2, 0, P(dark ? C(188, 192, 204) : C(250, 252, 255), dark ? C(140, 144, 158) : C(200, 208, 224), 255, 14, 42));
    }
    SHADOWED({
        shp body = RR(64, 78, 36, 38, 10);
        fill(cv, &body, 0, P(b0, b1, 255, 40, 116));
    });
    shp edge = STK(RR(64, 78, 36, 38, 10), U);
    fill(cv, &edge, 0, PA(WHITE, dark ? 70 : 170, 0, 40, 116));
    shp hd = RR(64, 24, 14, 6, 3), lid = RR(64, 36, 48, 8, 4);
    fill(cv, &hd, 0, P(l1, b1, 255, 18, 30));
    fill(cv, &lid, 0, P(l0, l1, 255, 28, 44));
    static const i32 sx[3] = { 50, 64, 78 };
    for (int i = 0; i < 3; i++) {
        shp l = CA(sx[i], 56, sx[i], 100, 3);
        fill(cv, &l, 0, P(sl, sl, dark ? 255 : 170, 0, S));
    }
}

/* Тёмная тема для плиток: все цвета на 20% темнее, белые глифы становятся мягко-серыми, как в macOS. */
static void tone_dark(u32 *cv)
{
    for (int i = 0; i < S * S; i++) {
        u32 p = cv[i];
        if (!(p >> 24)) continue;
        u32 r = (p >> 16 & 0xFF) * 205 / 255, g = (p >> 8 & 0xFF) * 205 / 255, b = (p & 0xFF) * 205 / 255;
        cv[i] = (p & 0xFF000000u) | r << 16 | g << 8 | b;
    }
}

int icon_style(void) { return g_style; }
void icon_set_style(int st) { g_style = st < 0 || st >= ICON_STYLE_COUNT ? 0 : st; }


static void ic_calc(u32 *cv)
{
    base(cv, C(92, 96, 106), C(30, 32, 38));
    SHADOWED({
        shp d = RR(64, 38, 38, 14, 6);
        fill(cv, &d, 0, P(C(222, 232, 224), C(176, 192, 178), 255, 24, 52));
    });
    static const i32 kx[3] = { 38, 64, 90 }, ky[3] = { 66, 86, 106 };
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            if (r == 2 && c == 2) continue;
            shp k = RR(kx[c], ky[r], 10, 7, 3);
            fill(cv, &k, 0, SOLID(c == 2 ? C(255, 159, 10) : C(176, 180, 190)));
        }
    shp eq = RR(90, 106, 10, 7, 3);
    fill(cv, &eq, 0, SOLID(C(255, 159, 10)));
}

static void ic_notes(u32 *cv)
{
    base(cv, C(255, 232, 120), C(255, 196, 40));
    SHADOWED({
        shp pg = RR(64, 70, 38, 44, 7);
        fill(cv, &pg, 0, P(WHITE, C(238, 240, 246), 255, 26, 114));
    });
    shp band = RR(64, 36, 38, 10, 7), clip = RR(64, 70, 38, 44, 7);
    fill(cv, &band, &clip, SOLID(C(255, 214, 70)));
    static const i32 ly[4] = { 58, 74, 90, 106 };
    for (int i = 0; i < 4; i++) {
        shp l = CA(40, ly[i], i == 3 ? 66 : 88, ly[i], 2);
        fill(cv, &l, 0, SOLID(C(190, 196, 210)));
    }
}

static void ic_clock(u32 *cv)
{
    base(cv, C(58, 58, 66), C(16, 16, 20));
    SHADOWED({
        shp d = CI(64, 64, 44);
        fill(cv, &d, 0, P(WHITE, C(226, 228, 236), 255, 20, 108));
    });
    static const i32 tx[4] = { 64, 64, 36, 92 }, ty[4] = { 28, 100, 64, 64 };
    for (int i = 0; i < 4; i++) {
        shp k = i < 2 ? CA(tx[i], i ? ty[i] - 3 : ty[i] + 3, tx[i], i ? ty[i] + 4 : ty[i] - 4, 1) : CA(i == 2 ? tx[i] - 3 : tx[i] + 3, ty[i], i == 2 ? tx[i] + 4 : tx[i] - 4, ty[i], 1);
        fill(cv, &k, 0, SOLID(C(120, 124, 138)));
    }
    shp h1 = CA(64, 64, 64, 42, 3), h2 = CA(64, 64, 80, 72, 3), sc = CA(64, 76, 64, 34, 1), dot = CI(64, 64, 4);
    fill(cv, &h1, 0, SOLID(C(30, 30, 36)));
    fill(cv, &h2, 0, SOLID(C(30, 30, 36)));
    fill(cv, &sc, 0, SOLID(C(255, 149, 0)));
    fill(cv, &dot, 0, SOLID(C(255, 149, 0)));
}

/* Календарь показывает настоящую дату: цифры рисуются штрихами, точки в шапке - дни недели (сегодняшний крупнее). */
static i32 g_day = 1, g_wday = 0;                              /* день месяца и день недели (пн = 0) */

static void poly(u32 *cv, const i32 *pt, int n, i32 x0, i32 y0, i32 w, i32 h, i32 r, u32 c, int flip)
{
    for (int i = 0; i + 1 < n; i++) {
        i32 ax = pt[2 * i], ay = pt[2 * i + 1], bx = pt[2 * i + 2], by = pt[2 * i + 3];
        if (flip) { ax = 100 - ax; ay = 100 - ay; bx = 100 - bx; by = 100 - by; }
        shp s = CA(x0 + ax * w / 100, y0 + ay * h / 100, x0 + bx * w / 100, y0 + by * h / 100, r);
        fill(cv, &s, 0, SOLID(c));
    }
}

static void digit(u32 *cv, int d, i32 x0, i32 y0, i32 w, i32 h, i32 r, u32 c)
{
    static const i32 d1[] = { 25, 22, 55, 0, 55, 100 };
    static const i32 d2[] = { 5, 25, 18, 6, 50, 0, 82, 6, 95, 28, 80, 52, 5, 100, 98, 100 };
    static const i32 d3a[] = { 8, 10, 45, 0, 85, 10, 95, 30, 72, 47, 40, 50 }, d3b[] = { 72, 49, 98, 70, 88, 92, 50, 100, 5, 90 };
    static const i32 d4[] = { 72, 100, 72, 0, 0, 70, 100, 70 };
    static const i32 d5[] = { 90, 0, 15, 0, 10, 45, 50, 38, 88, 55, 95, 78, 70, 98, 45, 100, 5, 90 };
    static const i32 d6[] = { 85, 5, 50, 0, 15, 20, 5, 60, 20, 95, 50, 100, 85, 90, 95, 68, 80, 48, 50, 42, 15, 58 };
    static const i32 d7[] = { 0, 0, 100, 0, 40, 100 };
    i32 cx = x0 + w / 2, rr = r;
    switch (d) {
    case 0: { shp o = STK(RR(cx, y0 + h / 2, w / 2 + r, h / 2 + r, w / 2 + r), 2 * r * U); fill(cv, &o, 0, SOLID(c)); break; }
    case 1: poly(cv, d1, 3, x0, y0, w, h, rr, c, 0); break;
    case 2: poly(cv, d2, 8, x0, y0, w, h, rr, c, 0); break;
    case 3: poly(cv, d3a, 6, x0, y0, w, h, rr, c, 0); poly(cv, d3b, 5, x0, y0, w, h, rr, c, 0); break;
    case 4: poly(cv, d4, 4, x0, y0, w, h, rr, c, 0); break;
    case 5: poly(cv, d5, 9, x0, y0, w, h, rr, c, 0); break;
    case 6: poly(cv, d6, 11, x0, y0, w, h, rr, c, 0); break;
    case 7: poly(cv, d7, 3, x0, y0, w, h, rr, c, 0); break;
    case 8: {
        i32 th = h * 46 / 100, bh = h * 54 / 100;
        shp a = STK(RR(cx, y0 + th / 2, w * 42 / 100 + r, th / 2 + r, w * 42 / 100 + r), 2 * r * U);
        shp b = STK(RR(cx, y0 + h - bh / 2, w / 2 + r, bh / 2 + r, w / 2 + r), 2 * r * U);
        fill(cv, &a, 0, SOLID(c)); fill(cv, &b, 0, SOLID(c)); break; }
    default: poly(cv, d6, 11, x0, y0, w, h, rr, c, 1); break;                /* 9 = перевёрнутая 6 */
    }
}

static void ic_calendar(u32 *cv)
{
    base(cv, C(255, 255, 255), C(232, 234, 240));
    shp band = RR(64, 34, 52, 16, 8), clip = RR(64, 64, 52, 52, 14);
    fill(cv, &band, &clip, P(C(255, 98, 84), C(236, 52, 62), 255, 18, 50));
    shp top = RR(64, 40, 52, 6, 0);
    fill(cv, &top, &clip, P(C(240, 62, 66), C(236, 52, 62), 255, 34, 46));
    for (int k = 0; k < 7; k++) {
        shp dot = CI(28 + k * 12, 33, k == g_wday ? 5 : 3);
        fill(cv, &dot, 0, PA(WHITE, k == g_wday ? 255 : 120, k == g_wday ? 255 : 120, 0, S));
    }
    u32 ink = C(38, 38, 46);
    i32 d = g_day < 1 ? 1 : g_day > 31 ? 31 : g_day, h = 46, t = 4;
    if (d < 10) digit(cv, d, 64 - 14, 58, 28, h, t, ink);
    else { digit(cv, d / 10, 64 - 33, 58, 26, h, t, ink); digit(cv, d % 10, 64 + 7, 58, 26, h, t, ink); }
}

static void ic_music(u32 *cv)
{
    base(cv, C(255, 94, 120), C(220, 30, 80));
    shp h1 = CI(50, 92, 14), h2 = CI(88, 84, 14), s1 = CA(61, 92, 61, 44, 3), s2 = CA(99, 84, 99, 36, 3), bm = CA(61, 44, 99, 36, 5);
    SHADOWED({
        fill(cv, &h1, 0, SOLID(C(255, 255, 255)));
        fill(cv, &h2, 0, SOLID(C(255, 255, 255)));
        fill(cv, &s1, 0, SOLID(C(255, 255, 255)));
        fill(cv, &s2, 0, SOLID(C(255, 255, 255)));
        fill(cv, &bm, 0, SOLID(C(255, 255, 255)));
    });
}

void icon_render(int id, u32 *cv, u32 accent, int dark)
{
    for (int i = 0; i < S * S; i++) cv[i] = 0;
    int is_tile = id <= ICON_LAUNCHPAD || (id >= ICON_TILE_WALL && id <= ICON_TILE_GLASS) || id == ICON_APP_CALC || id == ICON_APP_NOTES || id == ICON_APP_CLOCK || id == ICON_APP_CAL || id == ICON_APP_MUSIC;
    g_accent = accent & 0xFFFFFF;
    g_tone = is_tile && g_style >= ICON_STYLE_MONO ? g_style : 0;
    int sty = g_style; if (!is_tile) g_style = 0;                /* значки рабочего стола и панелей не меняют стиль */
    switch (id) {
    case 0: ic_files(cv); break;
    case 1: ic_terminal(cv); break;
    case 2: ic_settings(cv); break;
    case 3: ic_snake(cv); break;
    case 4: ic_about(cv, accent, dark); break;
    case 5: ic_browser(cv); break;
    case 6: ic_launchpad(cv); break;
    case ICON_APP_CALC: ic_calc(cv); break;
    case ICON_APP_NOTES: ic_notes(cv); break;
    case ICON_APP_CLOCK: ic_clock(cv); break;
    case ICON_APP_CAL: ic_calendar(cv); break;
    case ICON_APP_MUSIC: ic_music(cv); break;
    case ICON_TILE_WALL: tile_icon(cv, 0); break;
    case ICON_TILE_APPEAR: tile_icon(cv, 1); break;
    case ICON_TILE_GLASS: tile_icon(cv, 2); break;
    case ICON_DESK_FOLDER: ic_desk_folder(cv, dark); break;
    case ICON_DESK_FILE: ic_desk_file(cv, accent, dark); break;
    case ICON_TRASH: ic_trash(cv, 0, dark); break;
    case ICON_TRASH_FULL: ic_trash(cv, 1, dark); break;
    default:
        if (id >= ICON_EMOJI0) { ic_emoji(cv, id - ICON_EMOJI0); break; }
        if (id >= ICON_SIDE_HOME && id <= ICON_SIDE_DISK) glyph(cv, id - ICON_SIDE_HOME, accent & 0xFFFFFF);
        break;
    }
    g_style = sty; g_tone = 0;
    if (dark && id != 4 && is_tile && g_style < ICON_STYLE_MONO) tone_dark(cv);
}

/* ---------- кэш и масштабирование ---------- */

#define M1 (ICON_S / 2)
#define M2 (ICON_S / 4)

static u32 pm0[ICON_COUNT][S * S];  static u8 al0[ICON_COUNT][S * S];      /* 128: цвет*альфа и альфа */
static u32 pm1[ICON_COUNT][M1 * M1]; static u8 al1[ICON_COUNT][M1 * M1];   /* 64 */
static u32 pm2[ICON_COUNT][M2 * M2]; static u8 al2[ICON_COUNT][M2 * M2];   /* 32 */
static int cache_key = -1;
static u8 built[ICON_COUNT];

void icon_set_date(int day, int wday)                  /* вызывает wm при смене даты: значок Calendar перерисуется */
{
    if (day == g_day && wday == g_wday) return;
    g_day = day; g_wday = wday;
    built[ICON_APP_CAL] = 0;
}

static void half(const u32 *pm, const u8 *al, int n, u32 *opm, u8 *oal)    /* n -> n/2, усреднение 2x2 */
{
    int h = n / 2;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < h; i++) {
            u32 r = 0, g = 0, b = 0, a = 0;
            for (int q = 0; q < 4; q++) {
                int k = (2 * j + (q >> 1)) * n + 2 * i + (q & 1);
                u32 p = pm[k];
                r += p >> 16 & 0xFF; g += p >> 8 & 0xFF; b += p & 0xFF; a += al[k];
            }
            opm[j * h + i] = ((r >> 2) << 16) | ((g >> 2) << 8) | (b >> 2);
            oal[j * h + i] = (u8)(a >> 2);
        }
}

static void cache_build(int a)                          /* значок строится при первом показе: смена темы не пересчитывает все сразу */
{
    static u32 cv[S * S];
    icon_render(a, cv, theme_accent(), theme_is_dark());
    for (int k = 0; k < S * S; k++) {
        u32 p = cv[k], al = p >> 24;
        pm0[a][k] = ((p >> 16 & 0xFF) * al / 255 << 16) | ((p >> 8 & 0xFF) * al / 255 << 8) | (p & 0xFF) * al / 255;
        al0[a][k] = (u8)al;
    }
    half(pm0[a], al0[a], S, pm1[a], al1[a]);
    half(pm1[a], al1[a], M1, pm2[a], al2[a]);
    built[a] = 1;
}

static u32 bil(const u32 *pm, const u8 *al, int n, i32 fx, i32 fy, int alpha_plane)
{
    i32 x0 = fx >> 8, y0 = fy >> 8, tx = fx & 255, ty = fy & 255;
    i32 x1 = x0 + 1 < n ? x0 + 1 : x0, y1 = y0 + 1 < n ? y0 + 1 : y0;
    i32 xs[4] = { x0, x1, x0, x1 }, ys[4] = { y0, y0, y1, y1 };
    u32 wgt[4] = { (u32)((256 - tx) * (256 - ty)), (u32)(tx * (256 - ty)), (u32)((256 - tx) * ty), (u32)(tx * ty) };
    if (alpha_plane) {
        u32 v = 0;
        for (int k = 0; k < 4; k++) v += al[ys[k] * n + xs[k]] * wgt[k];
        return v >> 16;
    }
    u32 r = 0, g = 0, b = 0;
    for (int k = 0; k < 4; k++) {
        u32 p = pm[ys[k] * n + xs[k]];
        r += (p >> 16 & 0xFF) * wgt[k];
        g += (p >> 8 & 0xFF) * wgt[k];
        b += (p & 0xFF) * wgt[k];
    }
    return ((r >> 16) << 16) | ((g >> 16) << 8) | (b >> 16);
}

void icon_draw(int id, i32 x, i32 y, i32 size)
{
    if (size < 2 || id < 0 || id >= ICON_COUNT) return;
    int key = g_style * 64 + (theme_is_dark() ? 16 : 0) + theme_accent_index();
    if (key != cache_key) {
        for (int i = 0; i < ICON_COUNT; i++) built[i] = 0;
        cache_key = key;
    }
    if (!built[id]) cache_build(id);
    int n = size > 72 ? S : size > 36 ? M1 : M2;
    const u32 *pm = n == S ? pm0[id] : n == M1 ? pm1[id] : pm2[id];
    const u8 *al = n == S ? al0[id] : n == M1 ? al1[id] : al2[id];
    u32 *fbuf = fb_buffer();
    u32 stride = fb_width();
    for (i32 j = 0; j < size; j++) {
        i32 Y = y + j;
        if (Y < 0 || Y >= (i32)fb_height()) continue;
        i32 fy = j * (n - 1) * 256 / (size - 1);
        for (i32 i = 0; i < size; i++) {
            i32 X = x + i;
            if (X < 0 || X >= (i32)stride) continue;
            i32 fx = i * (n - 1) * 256 / (size - 1);
            u32 a = bil(pm, al, n, fx, fy, 1);
            if (a == 0) continue;
            u32 c = bil(pm, al, n, fx, fy, 0);
            u32 d = fbuf[Y * stride + X], inv = 255 - a;
            u32 r = (c >> 16 & 0xFF) + (d >> 16 & 0xFF) * inv / 255;
            u32 g = (c >> 8 & 0xFF) + (d >> 8 & 0xFF) * inv / 255;
            u32 b = (c & 0xFF) + (d & 0xFF) * inv / 255;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            fbuf[Y * stride + X] = (r << 16) | (g << 8) | b;
        }
    }
}
