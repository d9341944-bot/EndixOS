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

static void fill(u32 *cv, const shp *s, const shp *clip, paint p)
{
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
    fill(cv, &t, 0, P(top, bot, 255, 2, 126));
    fill(cv, &t, 0, PA(WHITE, 70, 0, 2, 72));                  /* блик сверху */
    shp rim = STK(tile(), U + U / 2);
    fill(cv, &rim, 0, PA(WHITE, 130, 0, 2, 56));               /* светлая кромка */
    fill(cv, &rim, 0, PA(BLACK, 0, 70, 70, 126));              /* тёмная внизу */
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

/* ---- Files: папка ---- */
static void ic_files(u32 *cv)
{
    base(cv, C(96, 190, 255), C(26, 112, 240));
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

/* ---- значки на рабочем столе: папка и документ ---- */
static void ic_desk_folder(u32 *cv)
{
    SHADOWED({
        shp tab = RR(36, 30, 20, 10, 6), back = RR(64, 46, 48, 28, 8);
        fill(cv, &tab, 0, P(C(120, 190, 255), C(76, 154, 250), 255, 20, 40));
        fill(cv, &back, 0, P(C(120, 190, 255), C(70, 148, 248), 255, 18, 74));
    });
    shp front = RR(64, 74, 52, 34, 10);
    fill(cv, &front, 0, P(C(150, 214, 255), C(50, 140, 248), 255, 40, 108));
    shp fh = STK(RR(64, 74, 52, 34, 10), U);
    fill(cv, &fh, 0, PA(WHITE, 190, 0, 40, 64));
}

static void ic_desk_file(u32 *cv)
{
    SHADOWED({
        shp pg = RR(64, 64, 40, 54, 8);
        fill(cv, &pg, 0, P(C(255, 255, 255), C(222, 227, 238), 255, 10, 118));
    });
    shp edge = STK(RR(64, 64, 40, 54, 8), U);
    fill(cv, &edge, 0, P(C(200, 205, 220), C(160, 168, 188), 255, 10, 118));
    shp cut1 = ROT(RR(104, 10, 17, 17, 0), 2, 104, 10);
    cut(cv, &cut1, 0);
    shp fold = ROT(RR(88, 26, 12, 12, 3), 2, 88, 26), clip = RR(64, 64, 40, 54, 8);
    fill(cv, &fold, &clip, P(C(236, 240, 248), C(190, 198, 216), 255, 14, 40));
    static const i32 ly[4] = { 58, 72, 86, 100 };
    for (int i = 0; i < 4; i++) {
        shp l = CA(40, ly[i], i == 3 ? 66 : 88, ly[i], 3);
        fill(cv, &l, 0, SOLID(C(190, 198, 216)));
    }
}

void icon_render(int id, u32 *cv, u32 accent, int dark)
{
    for (int i = 0; i < S * S; i++) cv[i] = 0;
    switch (id) {
    case 0: ic_files(cv); break;
    case 1: ic_terminal(cv); break;
    case 2: ic_settings(cv); break;
    case 3: ic_snake(cv); break;
    case 4: ic_about(cv, accent, dark); break;
    case 5: ic_launchpad(cv); break;
    case ICON_TILE_WALL: tile_icon(cv, 0); break;
    case ICON_TILE_APPEAR: tile_icon(cv, 1); break;
    case ICON_TILE_GLASS: tile_icon(cv, 2); break;
    case ICON_DESK_FOLDER: ic_desk_folder(cv); break;
    case ICON_DESK_FILE: ic_desk_file(cv); break;
    default:
        if (id >= ICON_SIDE_HOME && id <= ICON_SIDE_DISK) glyph(cv, id - ICON_SIDE_HOME, accent & 0xFFFFFF);
        break;
    }
}

/* ---------- кэш и масштабирование ---------- */

#define M1 (ICON_S / 2)
#define M2 (ICON_S / 4)

static u32 pm0[ICON_COUNT][S * S];  static u8 al0[ICON_COUNT][S * S];      /* 128: цвет*альфа и альфа */
static u32 pm1[ICON_COUNT][M1 * M1]; static u8 al1[ICON_COUNT][M1 * M1];   /* 64 */
static u32 pm2[ICON_COUNT][M2 * M2]; static u8 al2[ICON_COUNT][M2 * M2];   /* 32 */
static int cache_key = -1;

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

static void cache_build(void)
{
    static u32 cv[S * S];
    for (int a = 0; a < ICON_COUNT; a++) {
        icon_render(a, cv, theme_accent(), theme_is_dark());
        for (int k = 0; k < S * S; k++) {
            u32 p = cv[k], al = p >> 24;
            pm0[a][k] = ((p >> 16 & 0xFF) * al / 255 << 16) | ((p >> 8 & 0xFF) * al / 255 << 8) | (p & 0xFF) * al / 255;
            al0[a][k] = (u8)al;
        }
        half(pm0[a], al0[a], S, pm1[a], al1[a]);
        half(pm1[a], al1[a], M1, pm2[a], al2[a]);
    }
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
    int key = (theme_is_dark() ? 16 : 0) + theme_accent_index();
    if (key != cache_key) {
        cache_build();
        cache_key = key;
    }
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
