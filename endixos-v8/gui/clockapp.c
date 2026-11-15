/* Clock: секундомер с кругами и таймер обратного отсчёта. */
#include "clockapp.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "ui.h"
#include "../kernel/event.h"

#define MAXLAP 8
#define PAD 20

static int tab;                                    /* 0 секундомер, 1 таймер */
static u32 now;
/* секундомер, сотые доли секунды */
static int sw_run; static u32 sw_base, sw_acc;     /* sw_base - момент запуска, sw_acc - накоплено */
static u32 laps[MAXLAP]; static int nlap;
/* таймер */
static int tm_run, tm_fired; static u32 tm_set, tm_left, tm_end;   /* в секундах / тиках */

static u32 sw_time(void) { return sw_acc + (sw_run ? now - sw_base : 0); }
static u32 tm_rem(void) { return tm_run ? (tm_end > now ? tm_end - now : 0) : tm_left * 100; }   /* сотые */

static char *put2(char *p, u32 v) { *p++ = (char)('0' + v / 10 % 10); *p++ = (char)('0' + v % 10); return p; }
static void fmt_cs(char *b, u32 cs)                /* мм:сс.сс */
{
    char *p = put2(b, cs / 6000); *p++ = ':'; p = put2(p, cs / 100 % 60); *p++ = '.'; p = put2(p, cs % 100); *p = 0;
}
static void fmt_hms(char *b, u32 s)
{
    char *p = put2(b, s / 3600); *p++ = ':'; p = put2(p, s / 60 % 60); *p++ = ':'; p = put2(p, s % 60); *p = 0;
}

static void big(i32 cx, i32 y, const char *s, u32 col)   /* крупные цифры в ячейках одной ширины */
{
    const i32 dw = 24, cw = 12;
    i32 w = 0;
    for (const char *q = s; *q; q++) w += (*q >= '0' && *q <= '9') ? dw : cw;
    i32 x = cx - w / 2;
    for (const char *q = s; *q; q++) {
        char c[2] = { *q, 0 };
        int dig = *q >= '0' && *q <= '9';
        i32 a = dig ? dw : cw;
        font_draw(x + (a - font_width(c, FONT_TITLE)) / 2, y, c, col, FONT_TITLE);
        x += a;
    }
}

/* геометрия */
#define TABY 14
#define TABW 200
#define BIGY 80
#define BTNY 150
#define BTNY_T 184                           /* на вкладке таймера кнопки ниже: между ними и цифрами - кнопки - / + */
static i32 btny(void) { return tab ? BTNY_T : BTNY; }
#define BTNW 100

static const char *const tabs[2] = { "Stopwatch", "Timer" };

static void act_start(void)
{
    if (tab == 0) {
        if (sw_run) { sw_acc += now - sw_base; sw_run = 0; } else { sw_base = now; sw_run = 1; }
    } else {
        if (tm_run) { tm_left = (tm_end > now ? tm_end - now : 0) / 100; tm_run = 0; }
        else if (tm_left > 0) { tm_end = now + tm_left * 100; tm_run = 1; tm_fired = 0; }
    }
}

static void act_reset(void)
{
    if (tab == 0) {
        if (sw_run) { if (nlap < MAXLAP) laps[nlap++] = sw_time(); }
        else { sw_acc = 0; nlap = 0; }
    } else { tm_run = 0; tm_left = tm_set; }
}

static void add_time(int dsec)
{
    if (tm_run) return;
    int v = (int)tm_left + dsec;
    if (v < 0) v = 0;
    if (v > 99 * 3600 + 3599) v = 99 * 3600 + 3599;
    tm_left = (u32)v; tm_set = tm_left;
}

int clk_key(i32 k)
{
    if (k == ' ' || k == '\n') { act_start(); return 1; }
    if (k == 'r' || k == 'l' || k == 'R' || k == 'L') { act_reset(); return 1; }
    if (k == '\t') { tab ^= 1; return 1; }
    return 0;
}

static int in(i32 px, i32 py, i32 x, i32 y, i32 w, i32 h) { return px >= x && px < x + w && py >= y && py < y + h; }

int clk_click(i32 x, i32 y, u32 ticks)
{
    now = ticks;
    i32 cw = CLK_W;
    if (in(x, y, (cw - TABW) / 2, TABY, TABW, 28)) { int t = x >= cw / 2; if (t != tab) tab = t; return 1; }
    if (in(x, y, cw / 2 - BTNW - 8, btny(), BTNW, 32)) { act_reset(); return 1; }
    if (in(x, y, cw / 2 + 8, btny(), BTNW, 32)) { act_start(); return 1; }
    if (tab == 1) {
        static const int d[6] = { 3600, 60, 1, -3600, -60, -1 };
        for (int i = 0; i < 6; i++) {
            i32 bx = cw / 2 - 114 + (i % 3) * 80 + (i % 3 == 2 ? 0 : 0), by = BIGY - 26 + (i / 3) * 70;
            if (in(x, y, bx - 16, by, 64, 22)) { add_time(d[i]); return 1; }
        }
        static const int pre[3] = { 60, 300, 600 };
        for (int i = 0; i < 3; i++)
            if (in(x, y, cw / 2 - 114 + i * 80 - 16, btny() + 48, 64, 24)) { if (!tm_run) { tm_left = tm_set = (u32)pre[i]; } return 1; }
    }
    return 0;
}

int clk_tick(u32 ticks, int visible)
{
    now = ticks;
    int ch = 0;
    if (tm_run && tm_end <= now) { tm_run = 0; tm_left = 0; tm_fired = 1; ch = 1; }
    if (visible && (sw_run || tm_run)) ch = 1;
    return ch;
}

int clk_fired(void) { int f = tm_fired == 1; if (f) tm_fired = 2; return f; }

void clk_draw(i32 x, i32 y, i32 w, i32 h)
{
    const theme_t *t = theme();
    fb_fill_rect(x, y, w, h, t->body);
    ui_segment(x + (w - TABW) / 2, y + TABY, TABW, tabs, 2, tab);
    char b[24];
    if (tab == 0) {
        fmt_cs(b, sw_time());
        big(x + w / 2, y + BIGY, b, t->text);
    } else {
        u32 rem = tm_rem();
        fmt_hms(b, (rem + 99) / 100);
        big(x + w / 2, y + BIGY, b, tm_left == 0 && !tm_run ? t->sub : t->text);
        static const char *const up[3] = { "+ h", "+ min", "+ sec" }, *const dn[3] = { "- h", "- min", "- sec" };
        for (int i = 0; i < 3; i++) {
            i32 bx = x + w / 2 - 114 + i * 80 - 16;
            ui_button(bx, y + BIGY - 26, 64, 22, up[i], !tm_run, 0);
            ui_button(bx, y + BIGY + 44, 64, 22, dn[i], !tm_run, 0);
        }
        static const char *const pn[3] = { "1 min", "5 min", "10 min" };
        for (int i = 0; i < 3; i++) ui_button(x + w / 2 - 114 + i * 80 - 16, y + btny() + 48, 64, 24, pn[i], !tm_run, 0);
    }
    int running = tab == 0 ? sw_run : tm_run;
    const char *l = tab == 0 ? (sw_run ? "Lap" : "Reset") : "Reset";
    ui_button(x + w / 2 - BTNW - 8, y + btny(), BTNW, 32, l, tab == 0 ? (sw_run ? nlap < MAXLAP : sw_acc > 0) : (tm_left != tm_set || tm_run), 0);
    ui_button(x + w / 2 + 8, y + btny(), BTNW, 32, running ? "Stop" : "Start", tab == 0 || tm_left > 0, 1);
    if (tab == 0) {
        for (int i = 0; i < nlap; i++) {
            i32 ly = y + btny() + 52 + i * 22;
            if (ly + 20 > y + h) break;
            char n[8], v[16]; char *p = n; *p++ = 'L'; *p++ = 'a'; *p++ = 'p'; *p++ = ' '; *p++ = (char)('1' + i); *p = 0;
            u32 d = laps[i] - (i ? laps[i - 1] : 0);
            fmt_cs(v, d);
            ui_hline(x + PAD, ly - 2, w - 2 * PAD);
            font_draw(x + PAD + 6, ly + 2, n, t->sub, FONT_REGULAR);
            font_draw(x + w - PAD - 6 - font_width(v, FONT_REGULAR), ly + 2, v, t->text, FONT_REGULAR);
        }
    }
}
