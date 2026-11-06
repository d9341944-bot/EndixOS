/* Виджеты рабочего стола в стиле macOS. Часы рисуются со сглаживанием: линия - расстояние до отрезка. */
#include "widgets.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "logo.h"
#include "anim.h"

#define WX 24
#define WW 148
#define GAP 12

static const i16 SIN2[181] = {0,9,18,27,36,45,54,63,71,80,89,98,107,116,125,134,143,151,160,169,178,187,195,204,213,222,230,239,248,256,265,274,282,291,299,308,316,325,333,342,350,359,367,375,384,392,400,408,416,425,433,441,449,457,465,473,481,489,496,504,512,520,527,535,543,550,558,565,573,580,587,595,602,609,616,623,630,637,644,651,658,665,672,679,685,692,698,705,711,718,724,730,737,743,749,755,761,767,773,779,784,790,796,801,807,812,818,823,828,834,839,844,849,854,859,864,868,873,878,882,887,891,896,900,904,908,912,916,920,924,928,932,935,939,943,946,949,953,956,959,962,965,968,971,974,977,979,982,984,987,989,991,994,996,998,1000,1002,1003,1005,1007,1008,1010,1011,1013,1014,1015,1016,1017,1018,1019,1020,1021,1022,1022,1023,1023,1023,1024,1024,1024,1024};

static const sysinfo_t *si;
static u32 t_h, t_m, t_d, t_mo, t_y = 2026, up_s;

enum { W_CLOCK, W_CAL, W_SYS, W_DATE, W_N };
static const char *const wname[W_N] = { "Clock", "Calendar", "System", "Date" };
static const char *const wdesc[W_N] = { "Analog clock", "Month at a glance", "CPU, memory, uptime", "Today's date" };
static const u32 wcol[W_N] = { RGB(255, 149, 0), RGB(255, 59, 48), RGB(88, 86, 214), RGB(255, 69, 58) };
static const i32 wh[W_N] = { 148, 156, 116, 148 };
static u8  on[W_N] = { 1, 1, 1, 0 };
static i32 vv[W_N];                  /* 0..1024: появление и исчезновение */
static i32 xx[W_N], yy[W_N];         /* где виджет нарисован сейчас, 1/16 пикселя */
static i32 tx_[W_N], ty_[W_N];       /* куда он должен встать (положение, которое выбрал пользователь) */
static int drag_i = -1, drag_dx, drag_dy, drag_ox, drag_oy;   /* перетаскивание: виджет, смещение хвата, прежнее место */
static int pos_dirty;
static u8  pl[W_N];                   /* виджет уже стоит на своём месте (при раскладке учитываются только такие) */
static int ed, ed_v;                 /* режим редактирования */

#define TOP_Y 40
#define EDGE 12

static i32 bottom_limit(void) { return (i32)fb_height() - 96; }       /* ниже - Dock */

static int overlap(int i, i32 x, i32 y)                               /* пересекается ли i на месте (x, y) с другим включённым виджетом */
{
    for (int k = 0; k < W_N; k++)
        if (k != i && pl[k] && x < tx_[k] + WW + 6 && x + WW + 6 > tx_[k] && y < ty_[k] + wh[k] + 6 && y + wh[i] + 6 > ty_[k]) return 1;
    return 0;
}

static void place(int i)                                              /* первое свободное место: колонки слева направо, сверху вниз */
{
    i32 W = (i32)fb_width(), lim = bottom_limit();
    for (i32 x = WX; x + WW <= W - EDGE; x += WW + GAP)
        for (i32 y = TOP_Y; y + wh[i] <= lim; y += 6)
            if (!overlap(i, x, y)) { tx_[i] = x; ty_[i] = y; pl[i] = 1; return; }
    tx_[i] = WX; ty_[i] = TOP_Y; pl[i] = 1;
}

void widgets_init(const sysinfo_t *info)
{
    si = info;
    for (int i = 0; i < W_N; i++) pl[i] = 0;
    for (int i = 0; i < W_N; i++) { vv[i] = on[i] ? 1024 : 0; if (on[i]) place(i); xx[i] = tx_[i] << 4; yy[i] = ty_[i] << 4; }
}
void widgets_set_time(u32 h, u32 m, u32 d, u32 mo, u32 y) { t_h = h; t_m = m; t_d = d; t_mo = mo; t_y = y; }
void widgets_set_uptime(u32 s) { up_s = s; }
int  widgets_edit_active(void) { return ed; }
void widgets_edit_start(void) { ed = 1; }
void widgets_edit_stop(void) { ed = 0; if (drag_i >= 0) widgets_edit_drag(0, 0, 0); }
int  widgets_get_mask(void) { int m = 0; for (int i = 0; i < W_N; i++) if (on[i]) m |= 1 << i; return m; }
void widgets_set_mask(int m)
{
    for (int i = 0; i < W_N; i++) { on[i] = (u8)(m >> i & 1); pl[i] = 0; }
    for (int i = 0; i < W_N; i++) { vv[i] = on[i] ? 1024 : 0; if (on[i]) place(i); xx[i] = tx_[i] << 4; yy[i] = ty_[i] << 4; }
}
void widgets_get_pos(u8 out[8])                   /* x/8 и y/8 каждого виджета (0 - не задано) */
{
    for (int i = 0; i < W_N && i < 4; i++) { out[i * 2] = on[i] ? (u8)(tx_[i] / 8) : 0; out[i * 2 + 1] = on[i] ? (u8)(ty_[i] / 8) : 0; }
}
void widgets_set_pos(const u8 in[8])
{
    i32 W = (i32)fb_width(), lim = bottom_limit();
    for (int i = 0; i < W_N && i < 4; i++) {
        if (!on[i] || !in[i * 2] || !in[i * 2 + 1]) continue;
        i32 x = in[i * 2] * 8, y = in[i * 2 + 1] * 8;
        if (x + WW > W - 4 || y < 28 || y + wh[i] > lim || overlap(i, x, y)) continue;      /* сохранено для другого экрана или наложилось */
        tx_[i] = x; ty_[i] = y; xx[i] = x << 4; yy[i] = y << 4;
    }
}
void widgets_remove(int i) { if (i >= 0 && i < W_N) { on[i] = 0; pl[i] = 0; } }
void widgets_add(int i)
{
    if (i < 0 || i >= W_N || on[i]) return;
    on[i] = 1;
    place(i);
    xx[i] = tx_[i] << 4; yy[i] = ty_[i] << 4;
}
int  widgets_visible(void)
{
    if (ed || ed_v > 0) return 1;
    for (int i = 0; i < W_N; i++) if (on[i] || vv[i] > 0) return 1;
    return 0;
}

int widgets_tick(u32 dt)
{
    int moving = 0;
    i32 n = anim_approach(ed_v, ed ? 1024 : 0, dt);
    moving |= n != ed_v || (n != 0 && n != 1024);
    ed_v = n;
    for (int i = 0; i < W_N; i++) {
        n = anim_approach(vv[i], on[i] ? 1024 : 0, dt);
        moving |= n != vv[i] || (n != 0 && n != 1024);
        vv[i] = n;
        if (!on[i] || i == drag_i) continue;
        i32 wx = tx_[i] << 4, wy_ = ty_[i] << 4;
        for (u32 k = 0; k < (dt > 12 ? 12 : dt) && (yy[i] != wy_ || xx[i] != wx); k++) {      /* плавная посадка на место */
            i32 d = wy_ - yy[i], st = d * 3 / 10;
            if (st == 0 && d) st = d > 0 ? 1 : -1;
            yy[i] += st;
            d = wx - xx[i]; st = d * 3 / 10;
            if (st == 0 && d) st = d > 0 ? 1 : -1;
            xx[i] += st;
        }
        if (yy[i] != wy_ || xx[i] != wx) moving = 1;
    }
    return moving;
}

/* ---------- примитивы ---------- */

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

static i32 sin_h(i32 i)                       /* i - полградусы по часовой, результат -1024..1024 */
{
    i %= 720;
    if (i < 0) i += 720;
    int neg = i >= 360;
    if (neg) i -= 360;
    if (i > 180) i = 360 - i;
    return neg ? -SIN2[i] : SIN2[i];
}
static i32 cos_h(i32 i) { return sin_h(i + 180); }

/* Отрезок с круглыми концами; координаты и полуширина - в 1/16 пикселя. */
static void aa_line(i32 x0, i32 y0, i32 x1, i32 y1, i32 hw, u32 col)
{
    i32 minx = (x0 < x1 ? x0 : x1) - hw - 16, maxx = (x0 > x1 ? x0 : x1) + hw + 16;
    i32 miny = (y0 < y1 ? y0 : y1) - hw - 16, maxy = (y0 > y1 ? y0 : y1) + hw + 16;
    i32 vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy;
    for (i32 py = miny >> 4; py <= maxy >> 4; py++)
        for (i32 px = minx >> 4; px <= maxx >> 4; px++) {
            i32 sx = px * 16 + 8 - x0, sy = py * 16 + 8 - y0;
            i32 t = 0;
            if (l2 > 0) {
                t = (((sx * vx + sy * vy) >> 4) * 256) / ((l2 >> 4) + 1);
                t = t < 0 ? 0 : t > 256 ? 256 : t;
            }
            i32 dx = sx - ((vx * t) >> 8), dy = sy - ((vy * t) >> 8);
            i32 d = (i32)isqrt((u32)(dx * dx + dy * dy));
            i32 c = hw + 8 - d;
            if (c <= 0) continue;
            fb_blend_pixel(px, py, col, c >= 16 ? 255 : (u32)c * 255 / 16);
        }
}

static int lift;                     /* виджет поднят пальцем: тень глубже */

static void card(i32 x, i32 y, i32 w, i32 h, u32 *text, u32 *sub)
{
    int dark = theme_is_dark();
    u32 bg = dark ? RGB(44, 44, 50) : RGB(252, 252, 254);
    *text = dark ? RGB(240, 240, 244) : RGB(28, 28, 32);
    *sub = dark ? RGB(150, 150, 160) : RGB(124, 124, 132);
    if (lift) fb_shadow(x, y, w, h, 16, 30, dark ? 120 : 80); else fb_shadow(x, y, w, h, 6, 16, dark ? 70 : 38);
    fb_fill_round_rect(x, y, w, h, 20, dark ? RGB(86, 86, 94) : RGB(214, 214, 220), 255);
    fb_fill_round_rect(x + 1, y + 1, w - 2, h - 2, 19, bg, 255);
}

static void center(i32 cx, i32 y, const char *s, u32 col, int style)
{
    font_draw(cx - font_width(s, style) / 2, y, s, col, style);
}

static char *put_n(char *p, u32 v, int pad)
{
    char t[12]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n < pad) t[n++] = '0';
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }

/* ---------- часы ---------- */

static void clock_widget(i32 x, i32 y)
{
    u32 tx, sb;
    card(x, y, WW, WW, &tx, &sb);
    i32 cx = x + WW / 2, cy = y + WW / 2;
    int dark = theme_is_dark();
    fb_fill_circle(cx, cy, 62, dark ? RGB(24, 24, 28) : RGB(240, 240, 244));
    fb_fill_circle(cx, cy, 60, dark ? RGB(34, 34, 40) : RGB(255, 255, 255));
    for (int k = 0; k < 60; k += 5) {                   /* риски часов */
        i32 a = k * 12;
        i32 r0 = (k % 15 == 0) ? 52 : 53, r1 = 57;
        aa_line(cx * 16 + sin_h(a) * r0 / 64, cy * 16 - cos_h(a) * r0 / 64,
                cx * 16 + sin_h(a) * r1 / 64, cy * 16 - cos_h(a) * r1 / 64, 14, k % 15 == 0 ? tx : sb);
    }
    center(cx, cy - 46, "12", tx, FONT_BOLD);
    center(cx + 40, cy - 8, "3", tx, FONT_BOLD);
    center(cx, cy + 28, "6", tx, FONT_BOLD);
    center(cx - 40, cy - 8, "9", tx, FONT_BOLD);
    i32 ha = (i32)(t_h % 12) * 60 + (i32)t_m, ma = (i32)t_m * 12;
    aa_line(cx * 16, cy * 16, cx * 16 + sin_h(ha) * 30 / 64, cy * 16 - cos_h(ha) * 30 / 64, 50, tx);
    aa_line(cx * 16, cy * 16, cx * 16 + sin_h(ma) * 44 / 64, cy * 16 - cos_h(ma) * 44 / 64, 34, tx);
    fb_fill_circle(cx, cy, 4, RGB(255, 149, 0));
}

/* ---------- календарь ---------- */

static const char *const month_name[12] = { "January", "February", "March", "April", "May", "June", "July",
                                            "August", "September", "October", "November", "December" };

static int dow(u32 y, u32 m, u32 d)                      /* 0 = понедельник */
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    int w = (int)((y + y / 4 - y / 100 + y / 400 + (u32)t[m - 1] + d) % 7);   /* 0 = воскресенье */
    return (w + 6) % 7;
}

static int mdays(u32 y, u32 m)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[m - 1];
}

static void calendar_widget(i32 x, i32 y)
{
    u32 tx, sb;
    card(x, y, WW, 156, &tx, &sb);
    u32 mo = t_mo >= 1 && t_mo <= 12 ? t_mo : 1;
    char buf[32];
    char *p = put_s(buf, month_name[mo - 1]);
    *p++ = ' ';
    put_n(p, t_y, 0);
    font_draw(x + 14, y + 10, buf, RGB(255, 59, 48), FONT_BOLD);
    static const char *const wd[7] = { "M", "T", "W", "T", "F", "S", "S" };
    i32 cw = (WW - 16) / 7;
    for (int i = 0; i < 7; i++)
        center(x + 8 + i * cw + cw / 2, y + 32, wd[i], i >= 5 ? RGB(255, 100, 90) : sb, FONT_REGULAR);
    int first = dow(t_y, mo, 1), nd = mdays(t_y, mo);
    for (int d = 1; d <= nd; d++) {
        int k = first + d - 1;
        i32 cx = x + 8 + (k % 7) * cw + cw / 2, cy = y + 59 + (k / 7) * 17;
        put_n(buf, (u32)d, 0);
        if ((u32)d == t_d) {
            fb_fill_circle(cx, cy, 8, RGB(255, 59, 48));
            center(cx, cy - 8, buf, RGB(255, 255, 255), FONT_BOLD);
        } else {
            center(cx, cy - 8, buf, k % 7 >= 5 ? sb : tx, FONT_REGULAR);
        }
    }
}

/* ---------- система ---------- */

static void trim_to(char *s, i32 maxw)
{
    int n = 0;
    while (s[n]) n++;
    while (n > 1 && font_width(s, FONT_REGULAR) > maxw) {
        n--;
        while (n > 1 && ((u8)s[n] & 0xC0) == 0x80) n--;
        s[n] = 0;
    }
}

static void system_widget(i32 x, i32 y)
{
    u32 tx, sb;
    card(x, y, WW, 116, &tx, &sb);
    logo_draw(x + 14, y + 12, 18, theme_accent());
    font_draw(x + 38, y + 12, "EndixOS 8.0", tx, FONT_BOLD);
    char buf[64], *p;
    i32 ry = y + 42;
    font_draw(x + 14, ry, "CPU", sb, FONT_REGULAR);
    put_s(buf, si ? si->cpu : "?");
    trim_to(buf, WW - 28);
    font_draw(x + 14, ry + 16, buf, tx, FONT_REGULAR);
    font_draw(x + 14, ry + 38, "Memory", sb, FONT_REGULAR);
    p = put_n(buf, si ? si->ram_mb : 0, 0);
    put_s(p, " MB");
    font_draw(x + WW - 14 - font_width(buf, FONT_REGULAR), ry + 38, buf, tx, FONT_REGULAR);
    font_draw(x + 14, ry + 56, "Uptime", sb, FONT_REGULAR);
    p = put_n(buf, up_s / 60, 0);
    *p++ = ':';
    put_n(p, up_s % 60, 2);
    font_draw(x + WW - 14 - font_width(buf, FONT_REGULAR), ry + 56, buf, tx, FONT_REGULAR);
}

static int dow2(u32 y, u32 m, u32 d)                      /* 0 = понедельник */
{
    return dow(y, m, d);
}

static void date_widget(i32 x, i32 y)
{
    u32 tx, sb;
    card(x, y, WW, WW, &tx, &sb);
    static const char *const wdn[7] = { "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY" };
    u32 mo = t_mo >= 1 && t_mo <= 12 ? t_mo : 1;
    center(x + WW / 2, y + 16, wdn[dow2(t_y, mo, t_d ? t_d : 1)], RGB(255, 59, 48), FONT_BOLD);
    char b[16];
    put_n(b, t_d, 0);
    center(x + WW / 2, y + 46, b, tx, FONT_TITLE);
    center(x + WW / 2, y + 100, month_name[mo - 1], sb, FONT_REGULAR);
}

static void draw_one(int i, i32 x, i32 y)
{
    switch (i) {
    case W_CLOCK: clock_widget(x, y); break;
    case W_CAL:   calendar_widget(x, y); break;
    case W_SYS:   system_widget(x, y); break;
    default:      date_widget(x, y); break;
    }
}

static i32 wy(int i) { return yy[i] >> 4; }
static i32 wxp(int i) { return xx[i] >> 4; }

int widgets_hit(i32 px, i32 py)
{
    for (int i = 0; i < W_N; i++)
        if (on[i] && px >= wxp(i) && px < wxp(i) + WW && py >= wy(i) && py < wy(i) + wh[i]) return i;
    return -1;
}

static void minus_button(i32 cx, i32 cy)
{
    fb_fill_circle(cx, cy, 12, RGB(0, 0, 0));
    fb_fill_circle(cx, cy, 11, RGB(120, 120, 128));
    fb_fill_rect(cx - 5, cy - 1, 11, 3, RGB(255, 255, 255));
}

/* late = 1: рисуем поверх окон (режим редактирования), иначе под окнами. */
void widgets_draw(int late)
{
    int editing = ed || ed_v > 0;
    if (late != editing) return;
    i32 H = (i32)fb_height(), W = (i32)fb_width();
    if (editing)
        fb_blend_rect(0, 24, W, H - 24, 0, (u32)(110 * ed_v / 1024));
    for (int pass = 0; pass < 2; pass++)                       /* вторым проходом - поднятый виджет, поверх остальных */
    for (int i = 0; i < W_N; i++) {
        if (!on[i] && vv[i] <= 0) continue;
        if ((i == drag_i) != pass) continue;
        i32 x = wxp(i), y = wy(i);
        if (y + wh[i] >= H - 90 && i != drag_i) continue;
        int anim = vv[i] < 1024;
        lift = i == drag_i;
        if (anim) anim_region_begin(x - 24, y - 24, WW + 48, wh[i] + 48);
        draw_one(i, x, y);
        if (anim) anim_region_end(-(1024 - vv[i]) * 14 / 1024, vv[i], 0);
        lift = 0;
        if (ed && on[i] && !lift && i != drag_i) minus_button(x, y);
    }
    if (!editing) return;

    /* галерея: что можно добавить */
    int off[W_N], n = 0;
    for (int i = 0; i < W_N; i++) if (!on[i]) off[n++] = i;
    const theme_t *t = theme();
    i32 gw = 312, rows = n ? n : 1, gh = 52 + rows * 62 + 56;
    i32 gx = W - gw - 18 + (1024 - ed_v) * (gw + 30) / 1024, gy = 24 + 24;
    fb_glass(gx, gy, gw, gh, 22, GLASS_ALL, 12, theme_is_dark() ? RGB(40, 40, 46) : RGB(246, 246, 249),
             theme_is_dark() ? 160 : 175, 26);
    font_draw(gx + 20, gy + 16, "Widgets", t->text, FONT_TITLE);
    if (!n) {
        font_draw(gx + 20, gy + 66, "All widgets are on the desktop.", t->sub, FONT_REGULAR);
    }
    for (int k = 0; k < n; k++) {
        i32 ry = gy + 52 + k * 62;
        fb_fill_round_rect(gx + 16, ry + 6, 44, 44, 12, wcol[off[k]], 255);
        char l[2] = { wname[off[k]][0], 0 };
        center(gx + 38, ry + 18, l, RGB(255, 255, 255), FONT_BOLD);
        font_draw(gx + 72, ry + 10, wname[off[k]], t->text, FONT_BOLD);
        font_draw(gx + 72, ry + 30, wdesc[off[k]], t->sub, FONT_REGULAR);
        fb_fill_circle(gx + gw - 34, ry + 28, 13, RGB(52, 199, 89));
        fb_fill_rect(gx + gw - 34 - 6, ry + 27, 13, 3, RGB(255, 255, 255));
        fb_fill_rect(gx + gw - 35, ry + 28 - 6, 3, 13, RGB(255, 255, 255));
    }
    i32 by = gy + gh - 44;
    fb_fill_round_rect(gx + gw - 20 - 84, by, 84, 30, 9, theme_accent(), 255);
    center(gx + gw - 20 - 42, by + 5, "Done", RGB(255, 255, 255), FONT_BOLD);
}

/* Нажатие в режиме редактирования: убрать, добавить, готово. Возвращает 1, если нажатие использовано. */
int widgets_edit_press(i32 px, i32 py)
{
    if (!ed) return 0;
    for (int i = 0; i < W_N; i++)
        if (on[i]) {
            i32 cx = wxp(i), cy = wy(i);
            if ((px - cx) * (px - cx) + (py - cy) * (py - cy) <= 14 * 14) { widgets_remove(i); return 1; }
        }
    int off[W_N], n = 0;
    for (int i = 0; i < W_N; i++) if (!on[i]) off[n++] = i;
    i32 W = (i32)fb_width();
    i32 gw = 312, rows = n ? n : 1, gh = 52 + rows * 62 + 56, gx = W - gw - 18, gy = 48;
    for (int k = 0; k < n; k++) {
        i32 ry = gy + 52 + k * 62;
        if (px >= gx + 8 && px < gx + gw - 8 && py >= ry && py < ry + 60) { widgets_add(off[k]); return 1; }
    }
    i32 by = gy + gh - 44;
    if (px >= gx + gw - 104 && px < gx + gw - 20 && py >= by && py < by + 30) { ed = 0; return 1; }
    if (px >= gx && px < gx + gw && py >= gy && py < gy + gh) return 1;       /* внутри галереи, мимо кнопок */
    int h = widgets_hit(px, py);
    if (h >= 0) {                                                              /* взяли виджет: тащим */
        drag_i = h; drag_dx = px - wxp(h); drag_dy = py - wy(h); drag_ox = tx_[h]; drag_oy = ty_[h];
        return 1;
    }
    ed = 0;                                                                    /* клик по пустому месту: готово */
    return 1;
}

/* Движение мыши в режиме редактирования: тащим виджет, на отпускании кладём на место.
 * Возвращает 1, если картинку нужно обновить. */
int widgets_edit_drag(i32 px, i32 py, int down)
{
    if (drag_i < 0) return 0;
    int i = drag_i;
    if (down) {
        i32 W = (i32)fb_width(), H = (i32)fb_height();
        i32 x = px - drag_dx, y = py - drag_dy;
        if (x < 4) x = 4;
        if (x > W - WW - 4) x = W - WW - 4;
        if (y < 28) y = 28;
        if (y > H - wh[i] - 8) y = H - wh[i] - 8;
        xx[i] = x << 4; yy[i] = y << 4;
        return 1;
    }
    /* отпустили: притягиваем к сетке и к левому краю, при наложении или заходе под Dock - назад */
    i32 x = wxp(i), y = wy(i), g = 6;
    x = (x + g / 2) / g * g; y = (y + g / 2) / g * g;
    if (x > WX - 14 && x < WX + 14) x = WX;                                     /* магнит к привычной колонке */
    i32 W = (i32)fb_width();
    if (x + WW > W - 4) x = W - WW - 4;
    if (y < 32) y = 32;
    int offn = 0; for (int k = 0; k < W_N; k++) if (!on[k]) offn++;
    i32 gw = 312, gh = 52 + (offn ? offn : 1) * 62 + 56, gx = W - gw - 18, gy = 48;      /* галерея справа: под неё не кладём */
    int under_gallery = x + WW > gx && x < gx + gw && y + wh[i] > gy && y < gy + gh;
    if (y + wh[i] > bottom_limit() || overlap(i, x, y) || under_gallery) { x = drag_ox; y = drag_oy; }
    else if (x != drag_ox || y != drag_oy) pos_dirty = 1;
    tx_[i] = x; ty_[i] = y;
    drag_i = -1;
    return 1;
}
int widgets_dragging(void) { return drag_i >= 0; }
int widgets_pos_dirty(void) { int d = pos_dirty; pos_dirty = 0; return d; }
