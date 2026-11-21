/* Виджеты рабочего стола в стиле macOS. Часы рисуются со сглаживанием: линия - расстояние до отрезка. */
#include "settings.h"
#include "widgets.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "logo.h"
#include "anim.h"
#include "battery.h"
#include "music.h"
#include "notes.h"
#include "wifi.h"
#include "timeset.h"

#define WX 24
#define WW 148
#define GAP 12

static const i16 SIN2[181] = {0,9,18,27,36,45,54,63,71,80,89,98,107,116,125,134,143,151,160,169,178,187,195,204,213,222,230,239,248,256,265,274,282,291,299,308,316,325,333,342,350,359,367,375,384,392,400,408,416,425,433,441,449,457,465,473,481,489,496,504,512,520,527,535,543,550,558,565,573,580,587,595,602,609,616,623,630,637,644,651,658,665,672,679,685,692,698,705,711,718,724,730,737,743,749,755,761,767,773,779,784,790,796,801,807,812,818,823,828,834,839,844,849,854,859,864,868,873,878,882,887,891,896,900,904,908,912,916,920,924,928,932,935,939,943,946,949,953,956,959,962,965,968,971,974,977,979,982,984,987,989,991,994,996,998,1000,1002,1003,1005,1007,1008,1010,1011,1013,1014,1015,1016,1017,1018,1019,1020,1021,1022,1022,1023,1023,1023,1024,1024,1024,1024};


#define WS 148
#define GAP 12
#define WX 24
#define TOP_Y 40
#define EDGE 12
#define NMAX 11

enum { T_CLOCK, T_CAL, T_SYS, T_BAT, T_MUSIC, T_NOTES, T_WIFI, T_WORLD, T_N };
enum { SZ_S, SZ_M, SZ_L };
static const char *const tname[T_N] = { "Clock", "Calendar", "System", "Battery", "Music", "Notes", "Wi-Fi", "World Clock" };
static const char *const tdesc[T_N] = { "Analog and digital time", "Date and month at a glance", "CPU, memory, uptime", "Charge level and power",
                                        "What is playing now", "Your latest note", "Network and signal", "Time around the world" };
static const u32 tcol[T_N] = { RGB(255, 149, 0), RGB(255, 59, 48), RGB(88, 86, 214), RGB(52, 199, 89),
                               RGB(250, 45, 108), RGB(255, 204, 0), RGB(10, 132, 255), RGB(48, 176, 199) };
static const u8 tsizes[T_N] = { 3, 7, 3, 3, 3, 6, 3, 2 };            /* какие размеры бывают: бит 0 малый, 1 средний, 2 большой */
static const char *const szname[3] = { "Small", "Medium", "Large" };

static i32 sw(int sz) { return sz == SZ_S ? WS : 2 * WS + GAP; }
static i32 sh(int sz) { return sz == SZ_L ? 2 * WS + GAP : WS; }

static const sysinfo_t *si;
static u32 t_h, t_m, t_d, t_mo, t_y = 2026, up_s;

typedef struct { u8 on, type, size, pl; i32 v, x, y, tx, ty; } inst_t;   /* x, y - где нарисован (1/16 пикселя); tx, ty - куда встать */
static inst_t w[NMAX];
static int drag_i = -1, drag_dx, drag_dy, drag_ox, drag_oy;
static int pos_dirty, ed, ed_v;
static int sel_t, sel_s = SZ_S;                                          /* выбранное в галерее */
static int lift;

static i32 bottom_limit(void) { return (i32)fb_height() - 96; }

static int overlap(int i, i32 x, i32 y, int sz)
{
    for (int k = 0; k < NMAX; k++) {
        if (k == i || !w[k].on || !w[k].pl) continue;
        if (x < w[k].tx + sw(w[k].size) + 6 && x + sw(sz) + 6 > w[k].tx && y < w[k].ty + sh(w[k].size) + 6 && y + sh(sz) + 6 > w[k].ty) return 1;
    }
    return 0;
}

static void place(int i)                                                 /* первое свободное место: колонки слева направо, сверху вниз */
{
    i32 W = (i32)fb_width(), lim = bottom_limit();
    int sz = w[i].size;
    for (i32 x = WX; x + sw(sz) <= W - EDGE; x += WS + GAP)
        for (i32 y = TOP_Y; y + sh(sz) <= lim; y += 6)
            if (!overlap(i, x, y, sz)) { w[i].tx = x; w[i].ty = y; w[i].pl = 1; return; }
    w[i].tx = WX; w[i].ty = TOP_Y; w[i].pl = 1;
}

static int add_inst(int type, int size)
{
    if (type < 0 || type >= T_N || size < 0 || size > 2 || !(tsizes[type] >> size & 1)) return -1;
    int k = -1;
    for (int i = 0; i < NMAX; i++) if (!w[i].on && w[i].v <= 0) { k = i; break; }
    if (k < 0) for (int i = 0; i < NMAX; i++) if (!w[i].on) { k = i; break; }
    if (k < 0) return -1;
    w[k].on = 1; w[k].type = (u8)type; w[k].size = (u8)size; w[k].pl = 0; w[k].v = 0;
    place(k);
    w[k].x = w[k].tx << 4; w[k].y = w[k].ty << 4;
    pos_dirty = 1;
    return k;
}

int widgets_count(void) { int n = 0; for (int i = 0; i < NMAX; i++) n += w[i].on; return n; }

void widgets_init(const sysinfo_t *info)
{
    si = info;
    for (int i = 0; i < NMAX; i++) { w[i].on = 0; w[i].v = 0; w[i].pl = 0; }
    add_inst(T_CLOCK, SZ_S); add_inst(T_CAL, SZ_S); add_inst(T_SYS, SZ_M);
    for (int i = 0; i < NMAX; i++) if (w[i].on) w[i].v = 1024;
    pos_dirty = 0;
}
void widgets_set_time(u32 h, u32 m, u32 d, u32 mo, u32 y) { t_h = h; t_m = m; t_d = d; t_mo = mo; t_y = y; }
void widgets_set_uptime(u32 s) { up_s = s; }
int  widgets_edit_active(void) { return ed; }
void widgets_edit_start(void) { ed = 1; sel_t = 0; sel_s = SZ_S; }
void widgets_edit_stop(void) { ed = 0; if (drag_i >= 0) widgets_edit_drag(0, 0, 0); }

void widgets_save(u8 out[WIDGETS_SAVE])
{
    for (int i = 0; i < WIDGETS_SAVE; i++) out[i] = 0;
    out[0] = 0xA5;
    int n = 0;
    for (int i = 0; i < NMAX; i++) {
        if (!w[i].on) continue;
        int o = 1 + n * 3; n++;
        out[o] = (u8)((w[i].type + 1) | (w[i].size << 4));
        out[o + 1] = (u8)(w[i].tx / 8); out[o + 2] = (u8)(w[i].ty / 8);
    }
}

void widgets_load(const u8 in[WIDGETS_SAVE])
{
    if (in[0] != 0xA5) return;                                           /* настроек виджетов ещё нет - остаётся раскладка по умолчанию */
    i32 W = (i32)fb_width(), lim = bottom_limit();
    for (int i = 0; i < NMAX; i++) { w[i].on = 0; w[i].v = 0; w[i].pl = 0; }
    for (int n = 0; n < NMAX; n++) {
        int o = 1 + n * 3, b = in[o];
        int type = (b & 15) - 1, size = (b >> 4) & 3;
        if (type < 0 || type >= T_N || size > 2 || !(tsizes[type] >> size & 1)) continue;
        int k = -1; for (int i = 0; i < NMAX; i++) if (!w[i].on) { k = i; break; }
        if (k < 0) break;
        w[k].on = 1; w[k].type = (u8)type; w[k].size = (u8)size; w[k].pl = 0; w[k].v = 1024;
        i32 x = in[o + 1] * 8, y = in[o + 2] * 8;
        if (x && y && x + sw(size) <= W - 4 && y >= 28 && y + sh(size) <= lim && !overlap(k, x, y, size)) { w[k].tx = x; w[k].ty = y; w[k].pl = 1; }
        else place(k);
        w[k].x = w[k].tx << 4; w[k].y = w[k].ty << 4;
    }
    pos_dirty = 0;
}

void widgets_remove(int i) { if (i >= 0 && i < NMAX && w[i].on) { w[i].on = 0; w[i].pl = 0; if (drag_i == i) drag_i = -1; pos_dirty = 1; } }

int widgets_resize(int i, int size)
{
    if (i < 0 || i >= NMAX || !w[i].on || size < 0 || size > 2 || !(tsizes[w[i].type] >> size & 1) || size == w[i].size) return 0;
    int old = w[i].size; i32 ox = w[i].tx, oy = w[i].ty;
    w[i].size = (u8)size;
    i32 W = (i32)fb_width(), lim = bottom_limit();
    w[i].pl = 0;
    if (ox + sw(size) <= W - 4 && oy + sh(size) <= lim && !overlap(i, ox, oy, size)) { w[i].tx = ox; w[i].ty = oy; w[i].pl = 1; }
    else {
        place(i);
        if (!w[i].pl) { w[i].size = (u8)old; w[i].tx = ox; w[i].ty = oy; w[i].pl = 1; return 0; }
    }
    pos_dirty = 1;
    return 1;
}

int widgets_visible(void)
{
    if (ed || ed_v > 0) return 1;
    for (int i = 0; i < NMAX; i++) if (w[i].on || w[i].v > 0) return 1;
    return 0;
}

int widgets_tick(u32 dt)
{
    int moving = 0;
    i32 n = anim_approach(ed_v, ed ? 1024 : 0, dt);
    moving |= n != ed_v || (n != 0 && n != 1024);
    ed_v = n;
    for (int i = 0; i < NMAX; i++) {
        n = anim_approach(w[i].v, w[i].on ? 1024 : 0, dt);
        moving |= n != w[i].v || (n != 0 && n != 1024);
        w[i].v = n;
        if (!w[i].on || i == drag_i) continue;
        i32 wx = w[i].tx << 4, wy = w[i].ty << 4;
        for (u32 k = 0; k < (dt > 12 ? 12 : dt) && (w[i].y != wy || w[i].x != wx); k++) {      /* плавная посадка на место */
            i32 d = wy - w[i].y, st = d * 3 / 10;
            if (st == 0 && d) st = d > 0 ? 1 : -1;
            w[i].y += st;
            d = wx - w[i].x; st = d * 3 / 10;
            if (st == 0 && d) st = d > 0 ? 1 : -1;
            w[i].x += st;
        }
        if (w[i].y != wy || w[i].x != wx) moving = 1;
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

static const char *const month_name[12] = { "January", "February", "March", "April", "May", "June", "July",
                                            "August", "September", "October", "November", "December" };
static const char *const wdn[7] = { "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY" };

static int dow(u32 y, u32 m, u32 d)                      /* 0 = понедельник */
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    int wd = (int)((y + y / 4 - y / 100 + y / 400 + (u32)t[m - 1] + d) % 7);   /* 0 = воскресенье */
    return (wd + 6) % 7;
}
static int mdays(u32 y, u32 m)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[m - 1];
}
static u32 cur_mo(void) { return t_mo >= 1 && t_mo <= 12 ? t_mo : 1; }

/* Много строк текста с переносом по ширине. */
static void wrap_text(i32 x, i32 y, i32 wd, int maxl, const char *s, u32 col)
{
    char ln[100]; int n = 0, l = 0; i32 lw = 0;
    while (*s && l < maxl) {
        if (*s == '\n') { ln[n] = 0; font_draw(x, y + l * 17, ln, col, FONT_REGULAR); l++; n = 0; lw = 0; s++; continue; }
        int c = utf8_clen(s); if (c < 1) c = 1;
        char ch[5]; for (int k = 0; k < c && k < 4; k++) ch[k] = s[k]; ch[c < 4 ? c : 4] = 0;
        i32 cwid = font_width(ch, FONT_REGULAR);
        if (lw + cwid > wd || n + c >= 98) { ln[n] = 0; font_draw(x, y + l * 17, ln, col, FONT_REGULAR); l++; n = 0; lw = 0; if (l >= maxl) return; }
        for (int k = 0; k < c; k++) ln[n++] = s[k];
        lw += cwid; s += c;
    }
    if (n && l < maxl) { ln[n] = 0; font_draw(x, y + l * 17, ln, col, FONT_REGULAR); }
}

/* Дуга-кольцо: проценты по часовой от 12 часов. */
static void ring(i32 cx, i32 cy, i32 r, i32 hw, int pct, u32 col, u32 track)
{
    for (int a = 0; a < 720; a += 6)
        aa_line(cx * 16 + sin_h(a) * r / 64, cy * 16 - cos_h(a) * r / 64, cx * 16 + sin_h(a + 6) * r / 64, cy * 16 - cos_h(a + 6) * r / 64, hw, track);
    int end = pct * 720 / 100;
    for (int a = 0; a < end; a += 6) {
        int b = a + 6 > end ? end : a + 6;
        aa_line(cx * 16 + sin_h(a) * r / 64, cy * 16 - cos_h(a) * r / 64, cx * 16 + sin_h(b) * r / 64, cy * 16 - cos_h(b) * r / 64, hw, col);
    }
}

/* ---------- часы ---------- */

static void analog(i32 cx, i32 cy, i32 R, u32 tx, u32 sb, int nums, u32 hh, u32 mm, u32 accent)
{
    int dark = theme_is_dark();
    fb_fill_circle(cx, cy, R + 2, dark ? RGB(24, 24, 28) : RGB(240, 240, 244));
    fb_fill_circle(cx, cy, R, dark ? RGB(34, 34, 40) : RGB(255, 255, 255));
    for (int k = 0; k < 60; k += 5) {
        i32 a = k * 12, r0 = R * 87 / 100, r1 = R * 95 / 100;
        aa_line(cx * 16 + sin_h(a) * r0 / 64, cy * 16 - cos_h(a) * r0 / 64, cx * 16 + sin_h(a) * r1 / 64, cy * 16 - cos_h(a) * r1 / 64, R > 40 ? 14 : 9, k % 15 == 0 ? tx : sb);
    }
    if (nums) {
        i32 o = R * 70 / 100;
        center(cx, cy - o - 8, "12", tx, FONT_BOLD); center(cx + o, cy - 8, "3", tx, FONT_BOLD);
        center(cx, cy + o - 8, "6", tx, FONT_BOLD);  center(cx - o, cy - 8, "9", tx, FONT_BOLD);
    }
    i32 ha = (i32)(hh % 12) * 60 + (i32)mm, ma = (i32)mm * 12;
    aa_line(cx * 16, cy * 16, cx * 16 + sin_h(ha) * (R * 50 / 100) / 64, cy * 16 - cos_h(ha) * (R * 50 / 100) / 64, R * 5 / 6, tx);
    aa_line(cx * 16, cy * 16, cx * 16 + sin_h(ma) * (R * 73 / 100) / 64, cy * 16 - cos_h(ma) * (R * 73 / 100) / 64, R * 34 / 60, tx);
    fb_fill_circle(cx, cy, R > 40 ? 4 : 3, accent);
}

static char *hhmm(char *p, u32 h, u32 m) { p = put_n(p, h, 2); *p++ = ':'; return put_n(p, m, 2); }

static void clock_widget(i32 x, i32 y, int sz)
{
    u32 tx, sb;
    card(x, y, sw(sz), sh(sz), &tx, &sb);
    if (sz == SZ_S) { analog(x + WS / 2, y + WS / 2, 62, tx, sb, 1, t_h, t_m, RGB(255, 149, 0)); return; }
    analog(x + 76, y + 74, 58, tx, sb, 1, t_h, t_m, RGB(255, 149, 0));
    char b[16]; hhmm(b, t_h, t_m);
    i32 rx = x + 154 + (sw(sz) - 154) / 2;
    center(rx, y + 40, b, tx, FONT_TITLE);
    u32 mo = cur_mo();
    center(rx, y + 84, wdn[dow(t_y, mo, t_d ? t_d : 1)], RGB(255, 149, 0), FONT_BOLD);
    char *p = put_s(b, month_name[mo - 1]); *p++ = ' '; put_n(p, t_d, 0);
    center(rx, y + 104, b, sb, FONT_REGULAR);
}

/* ---------- календарь ---------- */

static void month_grid(i32 x, i32 y, i32 wd, i32 rowh, u32 tx, u32 sb, int head)
{
    static const char *const wl[7] = { "M", "T", "W", "T", "F", "S", "S" };
    i32 cw = wd / 7; char buf[16];
    i32 y0 = y;
    if (head) { for (int i = 0; i < 7; i++) center(x + i * cw + cw / 2, y, wl[i], i >= 5 ? RGB(255, 100, 90) : sb, FONT_REGULAR); y0 = y + rowh; }
    u32 mo = cur_mo();
    int first = dow(t_y, mo, 1), nd = mdays(t_y, mo);
    i32 rad = rowh / 2 - 1; if (rad > 11) rad = 11;
    for (int d = 1; d <= nd; d++) {
        int k = first + d - 1;
        i32 cx = x + (k % 7) * cw + cw / 2, cy = y0 + (k / 7) * rowh + rowh / 2;
        put_n(buf, (u32)d, 0);
        if ((u32)d == t_d) { fb_fill_circle(cx, cy, rad, RGB(255, 59, 48)); center(cx, cy - 8, buf, RGB(255, 255, 255), FONT_BOLD); }
        else center(cx, cy - 8, buf, k % 7 >= 5 ? sb : tx, FONT_REGULAR);
    }
}

static void date_block(i32 x, i32 y, i32 wd, u32 tx, u32 sb)
{
    u32 mo = cur_mo(); char b[16];
    center(x + wd / 2, y + 16, wdn[dow(t_y, mo, t_d ? t_d : 1)], RGB(255, 59, 48), FONT_BOLD);
    put_n(b, t_d, 0);
    center(x + wd / 2, y + 46, b, tx, FONT_TITLE);
    center(x + wd / 2, y + 100, month_name[mo - 1], sb, FONT_REGULAR);
}

static void calendar_widget(i32 x, i32 y, int sz)
{
    u32 tx, sb;
    card(x, y, sw(sz), sh(sz), &tx, &sb);
    if (sz == SZ_S) { date_block(x, y, WS, tx, sb); return; }
    u32 mo = cur_mo(); char buf[32];
    if (sz == SZ_M) {
        date_block(x, y, WS, tx, sb);
        month_grid(x + WS + 2, y + 12, WS + 4 + 0 * 1, 18, tx, sb, 1);
        return;
    }
    char *p = put_s(buf, month_name[mo - 1]); *p++ = ' '; put_n(p, t_y, 0);
    font_draw(x + 18, y + 14, buf, RGB(255, 59, 48), FONT_TITLE);
    month_grid(x + 12, y + 52, sw(sz) - 24, 36, tx, sb, 1);
}

/* ---------- система ---------- */

static void system_widget(i32 x, i32 y, int sz)
{
    u32 tx, sb;
    card(x, y, sw(sz), sh(sz), &tx, &sb);
    i32 W = sw(sz);
    logo_draw(x + 14, y + 12, 18, theme_accent());
    font_draw(x + 38, y + 12, "EndixOS " ENDIX_VERSION, tx, FONT_BOLD);
    char buf[64], *p;
    if (sz == SZ_S) {
        font_draw(x + 14, y + 50, "Build", sb, FONT_REGULAR); put_n(buf, ENDIX_BUILD, 0); font_draw(x + WS - 14 - font_width(buf, FONT_REGULAR), y + 50, buf, tx, FONT_REGULAR);
        font_draw(x + 14, y + 74, "Memory", sb, FONT_REGULAR); p = put_n(buf, si ? si->ram_mb : 0, 0); put_s(p, " MB"); font_draw(x + WS - 14 - font_width(buf, FONT_REGULAR), y + 74, buf, tx, FONT_REGULAR);
        font_draw(x + 14, y + 98, "Uptime", sb, FONT_REGULAR); p = put_n(buf, up_s / 60, 0); *p++ = ':'; put_n(p, up_s % 60, 2); font_draw(x + WS - 14 - font_width(buf, FONT_REGULAR), y + 98, buf, tx, FONT_REGULAR);
        return;
    }
    i32 ry = y + 46;
    font_draw(x + 14, ry, "CPU", sb, FONT_REGULAR);
    put_s(buf, si ? si->cpu : "?"); trim_to(buf, W - 28);
    font_draw(x + 14, ry + 16, buf, tx, FONT_REGULAR);
    font_draw(x + 14, ry + 40, "Memory", sb, FONT_REGULAR);
    p = put_n(buf, si ? si->ram_mb : 0, 0); put_s(p, " MB");
    font_draw(x + 14 + 70, ry + 40, buf, tx, FONT_REGULAR);
    font_draw(x + W / 2 + 6, ry + 40, "Uptime", sb, FONT_REGULAR);
    p = put_n(buf, up_s / 3600, 0); *p++ = ':'; p = put_n(p, up_s / 60 % 60, 2); *p++ = ':'; put_n(p, up_s % 60, 2);
    font_draw(x + W - 14 - font_width(buf, FONT_REGULAR), ry + 40, buf, tx, FONT_REGULAR);
    font_draw(x + 14, ry + 62, "Build", sb, FONT_REGULAR); put_n(buf, ENDIX_BUILD, 0);
    font_draw(x + 14 + 70, ry + 62, buf, tx, FONT_REGULAR);
    if (sz == SZ_L) {
        font_draw(x + 14, ry + 94, "EndixOS is an operating system written from scratch.", sb, FONT_REGULAR);
        font_draw(x + 14, ry + 112, "Kernel, drivers, GUI, browser, and games.", sb, FONT_REGULAR);
    }
}

/* ---------- батарея ---------- */

static void battery_widget(i32 x, i32 y, int sz)
{
    u32 tx, sb;
    card(x, y, sw(sz), sh(sz), &tx, &sb);
    int pct = bat_percent(), ac = bat_ac();
    int dark = theme_is_dark();
    u32 track = dark ? RGB(62, 62, 70) : RGB(228, 228, 234);
    u32 col = pct >= 0 && pct <= 15 && ac != 1 ? RGB(255, 69, 58) : RGB(52, 199, 89);
    char b[16];
    if (pct >= 0) { char *p = put_n(b, (u32)pct, 0); *p++ = '%'; *p = 0; } else put_s(b, "--");
    const char *st = pct < 0 ? "No battery" : ac == 1 ? "Charging" : ac == 0 ? "On battery" : "Battery";
    if (sz == SZ_S) {
        ring(x + WS / 2, y + 66, 44, 36, pct < 0 ? 0 : pct, col, track);
        center(x + WS / 2, y + 66 - 11, b, tx, FONT_TITLE);
        center(x + WS / 2, y + 118, st, sb, FONT_REGULAR);
        return;
    }
    ring(x + 74, y + 74, 50, 40, pct < 0 ? 0 : pct, col, track);
    center(x + 74, y + 74 - 11, b, tx, FONT_BOLD);
    font_draw(x + 154, y + 40, "Battery", sb, FONT_REGULAR);
    font_draw(x + 154, y + 60, st, tx, FONT_BOLD);
    font_draw(x + 154, y + 86, ac == 1 ? "Power adapter connected" : "Running on battery", sb, FONT_REGULAR);
}

/* ---------- музыка ---------- */

static void note_icon(i32 x, i32 y, i32 s)
{
    fb_fill_round_rect(x, y, s, s, s / 4, RGB(250, 45, 108), 255);
    fb_fill_circle(x + s * 36 / 100, y + s * 70 / 100, s / 9, RGB(255, 255, 255));
    fb_fill_circle(x + s * 66 / 100, y + s * 63 / 100, s / 9, RGB(255, 255, 255));
    fb_fill_rect(x + s * 36 / 100 + s / 9 - 2, y + s * 28 / 100, 3, s * 42 / 100, RGB(255, 255, 255));
    fb_fill_rect(x + s * 66 / 100 + s / 9 - 2, y + s * 21 / 100, 3, s * 42 / 100, RGB(255, 255, 255));
    fb_fill_rect(x + s * 36 / 100 + s / 9 - 2, y + s * 21 / 100 + 1, s * 30 / 100 + 3, 5, RGB(255, 255, 255));
}

static void music_widget(i32 x, i32 y, int sz)
{
    u32 tx, sb;
    card(x, y, sw(sz), sh(sz), &tx, &sb);
    int pct = 0; const char *sub = "";
    const char *ti = music_now(&pct, &sub);
    int play = music_playing();
    char b[48];
    put_s(b, ti[0] ? ti : "Not Playing");
    if (sz == SZ_S) {
        note_icon(x + 14, y + 14, 44);
        trim_to(b, WS - 28);
        font_draw(x + 14, y + 74, b, tx, FONT_BOLD);
        font_draw(x + 14, y + 94, play ? "Playing" : ti[0] ? "Paused" : "Open Music", sb, FONT_REGULAR);
        fb_fill_round_rect(x + 14, y + 122, WS - 28, 4, 2, theme_is_dark() ? RGB(70, 70, 78) : RGB(222, 222, 228), 255);
        if (pct > 0) fb_fill_round_rect(x + 14, y + 122, (WS - 28) * pct / 100 + 2, 4, 2, RGB(250, 45, 108), 255);
        return;
    }
    note_icon(x + 16, y + 16, 64);
    trim_to(b, sw(sz) - 108);
    font_draw(x + 94, y + 24, b, tx, FONT_BOLD);
    font_draw(x + 94, y + 46, ti[0] ? sub : "Pick a track in the Music app", sb, FONT_REGULAR);
    font_draw(x + 94, y + 68, play ? "Playing" : ti[0] ? "Paused" : "", RGB(250, 45, 108), FONT_REGULAR);
    fb_fill_round_rect(x + 16, y + 112, sw(sz) - 32, 5, 2, theme_is_dark() ? RGB(70, 70, 78) : RGB(222, 222, 228), 255);
    if (pct > 0) fb_fill_round_rect(x + 16, y + 112, (sw(sz) - 32) * pct / 100 + 2, 5, 2, RGB(250, 45, 108), 255);
}

/* ---------- заметки ---------- */

static void notes_widget(i32 x, i32 y, int sz)
{
    u32 tx, sb;
    card(x, y, sw(sz), sh(sz), &tx, &sb);
    fb_fill_round_rect(x + 14, y + 14, 22, 22, 6, RGB(255, 204, 0), 255);
    fb_fill_rect(x + 19, y + 21, 12, 2, RGB(255, 255, 255)); fb_fill_rect(x + 19, y + 26, 12, 2, RGB(255, 255, 255)); fb_fill_rect(x + 19, y + 31, 8, 2, RGB(255, 255, 255));
    font_draw(x + 44, y + 17, "Notes", RGB(255, 170, 0), FONT_BOLD);
    const char *t = notes_preview();
    int lines = sz == SZ_L ? 15 : 5;
    if (!t[0]) { font_draw(x + 16, y + 52, "No notes yet.", sb, FONT_REGULAR); font_draw(x + 16, y + 72, "Write one in the Notes app.", sb, FONT_REGULAR); return; }
    wrap_text(x + 16, y + 48, sw(sz) - 32, lines, t, tx);
}

/* ---------- Wi-Fi ---------- */

static void bars(i32 x, i32 y, int level, u32 on_c, u32 off_c)
{
    for (int i = 0; i < 4; i++) fb_fill_round_rect(x + i * 12, y + 30 - (i + 1) * 8, 8, (i + 1) * 8, 2, i < level ? on_c : off_c, 255);
}

static void wifi_widget(i32 x, i32 y, int sz)
{
    u32 tx, sb;
    card(x, y, sw(sz), sh(sz), &tx, &sb);
    int st = wifi_state(), lvl = 0; const char *name = "";
    if (st == WIFI_ON) {
        int n = wifi_net_count();
        for (int i = 0; i < n; i++) if (wifi_net_connected(i)) { name = wifi_net_name(i); lvl = wifi_net_signal(i); break; }
    }
    char b[40];
    const char *head = st == WIFI_NONE ? "No adapter" : st == WIFI_OFF ? "Wi-Fi off" : st == WIFI_ERROR ? "Wi-Fi error" : name[0] ? name : "Not connected";
    put_s(b, head);
    u32 onc = RGB(10, 132, 255), offc = theme_is_dark() ? RGB(70, 70, 78) : RGB(214, 214, 220);
    if (sz == SZ_S) {
        bars(x + 14, y + 20, name[0] ? lvl : 0, onc, offc);
        trim_to(b, WS - 28);
        font_draw(x + 14, y + 74, b, tx, FONT_BOLD);
        font_draw(x + 14, y + 96, name[0] ? "Connected" : "Wi-Fi", sb, FONT_REGULAR);
        return;
    }
    bars(x + 22, y + 34, name[0] ? lvl : 0, onc, offc);
    trim_to(b, sw(sz) - 120);
    font_draw(x + 90, y + 28, b, tx, FONT_BOLD);
    font_draw(x + 90, y + 50, name[0] ? "Connected" : wifi_message(), sb, FONT_REGULAR);
    char *p = put_n(b, (u32)(st == WIFI_ON ? wifi_net_count() : 0), 0); put_s(p, " networks nearby");
    font_draw(x + 90, y + 72, b, sb, FONT_REGULAR);
    font_draw(x + 22, y + 112, wifi_adapter()[0] ? wifi_adapter() : "No Wi-Fi adapter found", sb, FONT_REGULAR);
}

/* ---------- мировое время ---------- */

static void world_widget(i32 x, i32 y, int sz)
{
    u32 tx, sb;
    card(x, y, sw(sz), sh(sz), &tx, &sb);
    static const char *const city[4] = { "Local", "London", "Moscow", "Tokyo" };
    static const int off[4] = { 0, 0, 180, 540 };
    int loc = ts_tz(), base = (int)(t_h * 60 + t_m);
    i32 cw = sw(sz) / 4;
    for (int i = 0; i < 4; i++) {
        int m = base - loc + (i == 0 ? loc : off[i]);
        m = ((m % 1440) + 1440) % 1440;
        u32 h = (u32)(m / 60), mi = (u32)(m % 60);
        i32 cx = x + i * cw + cw / 2;
        analog(cx, y + 44, 27, tx, sb, 0, h, mi, RGB(48, 176, 199));
        center(cx, y + 82, city[i], tx, FONT_BOLD);
        char b[8]; hhmm(b, h, mi);
        center(cx, y + 104, b, sb, FONT_REGULAR);
        center(cx, y + 124, (h >= 6 && h < 20) ? "Day" : "Night", sb, FONT_REGULAR);
    }
}

static void draw_type(int type, int sz, i32 x, i32 y)
{
    switch (type) {
    case T_CLOCK: clock_widget(x, y, sz); break;
    case T_CAL:   calendar_widget(x, y, sz); break;
    case T_SYS:   system_widget(x, y, sz); break;
    case T_BAT:   battery_widget(x, y, sz); break;
    case T_MUSIC: music_widget(x, y, sz); break;
    case T_NOTES: notes_widget(x, y, sz); break;
    case T_WIFI:  wifi_widget(x, y, sz); break;
    default:      world_widget(x, y, sz); break;
    }
}

static i32 wxp(int i) { return w[i].x >> 4; }
static i32 wyp(int i) { return w[i].y >> 4; }

int widgets_hit(i32 px, i32 py)
{
    for (int i = 0; i < NMAX; i++)
        if (w[i].on && px >= wxp(i) && px < wxp(i) + sw(w[i].size) && py >= wyp(i) && py < wyp(i) + sh(w[i].size)) return i;
    return -1;
}

static void minus_button(i32 cx, i32 cy)
{
    fb_fill_circle(cx, cy, 12, RGB(0, 0, 0));
    fb_fill_circle(cx, cy, 11, RGB(120, 120, 128));
    fb_fill_rect(cx - 5, cy - 1, 11, 3, RGB(255, 255, 255));
}
static void plus_button(i32 cx, i32 cy, int enabled)
{
    fb_fill_circle(cx, cy, 13, RGB(0, 0, 0));
    fb_fill_circle(cx, cy, 12, enabled ? RGB(52, 199, 89) : RGB(150, 150, 156));
    fb_fill_rect(cx - 6, cy - 1, 13, 3, RGB(255, 255, 255));
    fb_fill_rect(cx - 1, cy - 6, 3, 13, RGB(255, 255, 255));
}

/* ---------- галерея ---------- */

#define LIST_W 156
static void gal_rect(i32 *gx, i32 *gy, i32 *gw, i32 *gh)
{
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    *gw = LIST_W + 2 * WS + GAP + 30; if (*gw > W - 20) *gw = W - 20;
    *gh = 110 + 2 * WS + GAP + 22; if (*gh > H - 24 - 90) *gh = H - 24 - 90;
    *gx = W - *gw - 12; *gy = 32;
}
static void fix_sel(void) { if (!(tsizes[sel_t] >> sel_s & 1)) { for (int s = 0; s < 3; s++) if (tsizes[sel_t] >> s & 1) { sel_s = s; break; } } }
static int is_full(void) { return widgets_count() >= NMAX; }

static void draw_gallery(void)
{
    i32 gx, gy, gw, gh, W = (i32)fb_width();
    gal_rect(&gx, &gy, &gw, &gh);
    const theme_t *t = theme();
    int dark = theme_is_dark();
    i32 off = (1024 - ed_v) * (gw + 30) / 1024;
    gx += off;
    fb_glass(gx, gy, gw, gh, 22, GLASS_ALL, 12, dark ? RGB(40, 40, 46) : RGB(246, 246, 249), dark ? 160 : 175, 26);
    font_draw(gx + 20, gy + 16, "Widgets", t->text, FONT_TITLE);
    i32 bx = gx + gw - 20 - 84, by = gy + 14;
    fb_fill_round_rect(bx, by, 84, 30, 9, theme_accent(), 255);
    center(bx + 42, by + 5, "Done", RGB(255, 255, 255), FONT_BOLD);
    for (int k = 0; k < T_N; k++) {
        i32 ry = gy + 62 + k * 40;
        if (ry + 38 > gy + gh) break;
        if (k == sel_t) fb_fill_round_rect(gx + 10, ry, LIST_W - 8, 36, 10, theme_accent(), 70);
        fb_fill_round_rect(gx + 18, ry + 5, 26, 26, 8, tcol[k], 255);
        char l[2] = { tname[k][0], 0 };
        center(gx + 31, ry + 9, l, RGB(255, 255, 255), FONT_BOLD);
        font_draw(gx + 52, ry + 9, tname[k], t->text, k == sel_t ? FONT_BOLD : FONT_REGULAR);
    }
    i32 px = gx + LIST_W + 8, pw = gw - LIST_W - 22;
    font_draw(px, gy + 62, tname[sel_t], t->text, FONT_BOLD);
    font_draw(px, gy + 82, tdesc[sel_t], t->sub, FONT_REGULAR);
    i32 sg = pw / 3;                                                     /* выбор размера */
    for (int s = 0; s < 3; s++) {
        int ok = tsizes[sel_t] >> s & 1;
        i32 sx = px + s * sg;
        if (s == sel_s) fb_fill_round_rect(sx + 1, gy + 104, sg - 2, 26, 8, theme_accent(), 255);
        center(sx + sg / 2, gy + 108, szname[s], s == sel_s ? RGB(255, 255, 255) : ok ? t->text : t->sub, s == sel_s ? FONT_BOLD : FONT_REGULAR);
    }
    i32 vx = px, vy = gy + 144;
    lift = 0;
    if (vy + sh(sel_s) + 8 <= gy + gh && vx + sw(sel_s) <= gx + gw - 6) draw_type(sel_t, sel_s, vx, vy);
    else font_draw(vx, vy, "Not enough room on this screen.", t->sub, FONT_REGULAR);
    plus_button(vx + 4, vy + 4, !is_full());
    if (is_full()) font_draw(px, gy + gh - 24, "The desktop is full. Remove a widget first.", t->sub, FONT_REGULAR);
    (void)W;
}

void widgets_draw(int late)
{
    int editing = ed || ed_v > 0;
    if (late != editing) return;
    i32 H = (i32)fb_height(), W = (i32)fb_width();
    if (editing) fb_blend_rect(0, 24, W, H - 24, 0, (u32)(110 * ed_v / 1024));
    for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < NMAX; i++) {
        if (!w[i].on && w[i].v <= 0) continue;
        if ((i == drag_i) != pass) continue;
        i32 x = wxp(i), y = wyp(i), ww = sw(w[i].size), hh = sh(w[i].size);
        if (y + hh >= H - 90 && i != drag_i) continue;
        int anim = w[i].v < 1024;
        lift = i == drag_i;
        if (anim) anim_region_begin(x - 24, y - 24, ww + 48, hh + 48);
        draw_type(w[i].type, w[i].size, x, y);
        if (anim) anim_region_end(-(1024 - w[i].v) * 14 / 1024, w[i].v, 0);
        lift = 0;
        if (ed && w[i].on && i != drag_i) minus_button(x, y);
    }
    if (editing) draw_gallery();
}

/* Нажатие в режиме редактирования. Возвращает 1, если нажатие использовано. */
int widgets_edit_press(i32 px, i32 py)
{
    if (!ed) return 0;
    for (int i = 0; i < NMAX; i++)
        if (w[i].on) {
            i32 cx = wxp(i), cy = wyp(i);
            if ((px - cx) * (px - cx) + (py - cy) * (py - cy) <= 14 * 14) { widgets_remove(i); return 1; }
        }
    i32 gx, gy, gw, gh;
    gal_rect(&gx, &gy, &gw, &gh);
    if (px >= gx && px < gx + gw && py >= gy && py < gy + gh) {
        if (px >= gx + gw - 104 && px < gx + gw - 20 && py >= gy + 14 && py < gy + 44) { widgets_edit_stop(); return 1; }
        if (px < gx + LIST_W) {
            int k = (py - (gy + 62)) / 40;
            if (py >= gy + 62 && k >= 0 && k < T_N) { sel_t = k; fix_sel(); }
            return 1;
        }
        i32 pxx = gx + LIST_W + 8, pw = gw - LIST_W - 22, sg = pw / 3;
        if (py >= gy + 104 && py < gy + 130 && px >= pxx && px < pxx + pw) {
            int s = (px - pxx) / sg; if (s > 2) s = 2;
            if (tsizes[sel_t] >> s & 1) sel_s = s;
            return 1;
        }
        i32 vy = gy + 144;
        if (px >= pxx && px < pxx + sw(sel_s) && py >= vy && py < vy + sh(sel_s)) {      /* нажали на образец: добавить */
            if (!is_full()) add_inst(sel_t, sel_s);
            return 1;
        }
        return 1;
    }
    int h = widgets_hit(px, py);
    if (h >= 0) {
        drag_i = h; drag_dx = px - wxp(h); drag_dy = py - wyp(h); drag_ox = w[h].tx; drag_oy = w[h].ty;
        return 1;
    }
    ed = 0;
    return 1;
}

int widgets_edit_key(i32 key)
{
    if (!ed) return 0;
    if (key == '\n' || key == '\r') { if (!is_full()) add_inst(sel_t, sel_s); return 1; }
    if (key == 0x101 || key == 0x102) {                                  /* вверх/вниз по списку (KEY_UP, KEY_DOWN) */
        sel_t = (sel_t + (key == 0x102 ? 1 : T_N - 1)) % T_N; fix_sel(); return 1;
    }
    if (key == 0x103 || key == 0x104) {                                  /* влево/вправо: размер */
        for (int n = 0; n < 3; n++) { sel_s = (sel_s + (key == 0x104 ? 1 : 2)) % 3; if (tsizes[sel_t] >> sel_s & 1) break; }
        return 1;
    }
    return 0;
}

int widgets_edit_drag(i32 px, i32 py, int down)
{
    if (drag_i < 0) return 0;
    int i = drag_i, sz = w[i].size;
    if (down) {
        i32 W = (i32)fb_width(), H = (i32)fb_height();
        i32 x = px - drag_dx, y = py - drag_dy;
        if (x < 4) x = 4;
        if (x > W - sw(sz) - 4) x = W - sw(sz) - 4;
        if (y < 28) y = 28;
        if (y > H - sh(sz) - 8) y = H - sh(sz) - 8;
        w[i].x = x << 4; w[i].y = y << 4;
        return 1;
    }
    i32 x = wxp(i), y = wyp(i), g = 6;
    x = (x + g / 2) / g * g; y = (y + g / 2) / g * g;
    if (x > WX - 14 && x < WX + 14) x = WX;
    i32 W = (i32)fb_width();
    if (x + sw(sz) > W - 4) x = W - sw(sz) - 4;
    if (y < 32) y = 32;
    i32 gx, gy, gw, gh;
    gal_rect(&gx, &gy, &gw, &gh);
    int under = ed && x + sw(sz) > gx && x < gx + gw && y + sh(sz) > gy && y < gy + gh;
    if (y + sh(sz) > bottom_limit() || overlap(i, x, y, sz) || under) { x = drag_ox; y = drag_oy; }
    else if (x != drag_ox || y != drag_oy) pos_dirty = 1;
    w[i].tx = x; w[i].ty = y;
    drag_i = -1;
    return 1;
}
int widgets_dragging(void) { return drag_i >= 0; }
int widgets_pos_dirty(void) { int d = pos_dirty; pos_dirty = 0; return d; }
