/* Калькулятор: десятичная арифметика с фиксированной точкой (4 знака), без float и без 64-битного деления из libgcc. */
#include "calc.h"
#include "fb.h"
#include "font.h"
#include "theme.h"

#define SC 10000LL                   /* масштаб: 4 знака после точки */
#define LIM 9999999999999LL          /* |значение| < 1e9 в масштабе */
#define BTN 56
#define GAP 8
#define PAD 16
#define DISP_H 100

typedef struct { const char *lab; char code; u8 kind; } key_t_;   /* kind 0 цифра, 1 функция, 2 операция */
static const key_t_ KEYS[5][4] = {
    { { "AC", 'c', 1 }, { "+/-", 'n', 1 }, { "%", '%', 1 }, { "/", '/', 2 } },
    { { "7", '7', 0 }, { "8", '8', 0 }, { "9", '9', 0 }, { "x", '*', 2 } },
    { { "4", '4', 0 }, { "5", '5', 0 }, { "6", '6', 0 }, { "-", '-', 2 } },
    { { "1", '1', 0 }, { "2", '2', 0 }, { "3", '3', 0 }, { "+", '+', 2 } },
    { { "0", '0', 0 }, { "", 0, 0 }, { ".", '.', 0 }, { "=", '=', 2 } },
};

static char entry[16]; static int en;       /* вводимое число */
static int  has_entry, err;
static i64  acc;
static char op;
static char pressed;                         /* подсветка выбранной операции */

static u64 udiv(u64 n, u64 d, u64 *rem)      /* деление сдвигами: в ядре нет __udivdi3 */
{
    u64 q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= (u64)1 << i; }
    }
    if (rem) *rem = r;
    return q;
}

static i64 iabs64(i64 v) { return v < 0 ? -v : v; }

static i64 parse_entry(void)
{
    i64 ip = 0, fp = 0; int neg = 0, i = 0, frac = 0, fd = 0;
    if (entry[0] == '-') { neg = 1; i = 1; }
    for (; i < en; i++) {
        if (entry[i] == '.') { frac = 1; continue; }
        if (!frac) ip = ip * 10 + (entry[i] - '0');
        else if (fd < 4) { fp = fp * 10 + (entry[i] - '0'); fd++; }
    }
    while (fd++ < 4) fp *= 10;
    i64 v = ip * SC + fp;
    return neg ? -v : v;
}

static void format(i64 v, char *out)
{
    char t[24]; int n = 0, p = 0;
    if (v < 0) { out[p++] = '-'; v = -v; }
    u64 rem, ipart = udiv((u64)v, (u64)SC, &rem);
    u64 x = ipart; char r[24]; int rl = 0;
    do { u64 q = udiv(x, 10, &rem); r[rl++] = (char)('0' + (int)rem); x = q; } while (x);
    while (rl) out[p++] = r[--rl];
    udiv((u64)v, (u64)SC, &rem);
    if (rem) {
        char f[5]; int fl = 4;
        u64 y = rem;
        for (int k = 3; k >= 0; k--) { u64 q = udiv(y, 10, &rem); f[k] = (char)('0' + (int)rem); y = q; }
        while (fl > 0 && f[fl - 1] == '0') fl--;
        out[p++] = '.';
        for (int k = 0; k < fl; k++) out[p++] = f[k];
    }
    out[p] = 0; (void)t; (void)n;
}

static i64 cur_value(void) { return has_entry ? parse_entry() : acc; }

static void compute(void)                    /* acc = acc op текущее */
{
    if (!op) { acc = cur_value(); return; }
    i64 a = acc, b = cur_value(), r = 0;
    u64 rem;
    switch (op) {
    case '+': r = a + b; break;
    case '-': r = a - b; break;
    case '*': {
        u64 ua = (u64)iabs64(a), ub = (u64)iabs64(b);
        if (ua && ub > udiv((u64)9000000000000000000LL, ua, 0)) { err = 1; return; }
        u64 p = udiv(ua * ub, (u64)SC, 0);
        r = (a < 0) != (b < 0) ? -(i64)p : (i64)p;
        break; }
    case '/': {
        if (b == 0) { err = 1; return; }
        u64 q = udiv((u64)iabs64(a) * (u64)SC, (u64)iabs64(b), &rem);
        r = (a < 0) != (b < 0) ? -(i64)q : (i64)q;
        break; }
    }
    if (iabs64(r) > LIM) { err = 1; return; }
    acc = r;
}

void calc_reset(void) { en = 0; entry[0] = 0; has_entry = 0; err = 0; acc = 0; op = 0; pressed = 0; }

static void digit(char c)
{
    if (err) calc_reset();
    if (!has_entry) { en = 0; entry[0] = 0; has_entry = 1; }
    int digits = 0; for (int i = 0; i < en; i++) if (entry[i] >= '0' && entry[i] <= '9') digits++;
    if (digits >= 9 && c != '.') return;
    int dot = 0; for (int i = 0; i < en; i++) if (entry[i] == '.') dot = 1;
    if (c == '.') { if (dot) return; if (en == 0 || (en == 1 && entry[0] == '-')) entry[en++] = '0'; }
    else if (en == 1 && entry[0] == '0') en = 0;
    else if (en == 2 && entry[0] == '-' && entry[1] == '0') en = 1;
    if (dot) { int fd = 0, seen = 0; for (int i = 0; i < en; i++) { if (seen) fd++; if (entry[i] == '.') seen = 1; } if (fd >= 4) return; }
    entry[en++] = c; entry[en] = 0;
    pressed = 0;
}

static void press(char c)
{
    if ((c >= '0' && c <= '9') || c == '.') { digit(c); return; }
    if (c == 'c') { calc_reset(); return; }
    if (err) return;
    if (c == 'n') {
        if (has_entry) { if (entry[0] == '-') { for (int i = 0; i < en; i++) entry[i] = entry[i + 1]; en--; } else if (en < 15) { for (int i = en; i > 0; i--) entry[i] = entry[i - 1]; entry[0] = '-'; en++; } entry[en] = 0; }
        else acc = -acc;
        return;
    }
    if (c == '%') {
        i64 v = cur_value(); u64 rem; u64 q = udiv((u64)iabs64(v), 100, &rem); v = v < 0 ? -(i64)q : (i64)q;
        if (has_entry) { char t[24]; format(v, t); en = 0; for (int i = 0; t[i] && en < 15; i++) entry[en++] = t[i]; entry[en] = 0; } else acc = v;
        return;
    }
    if (c == '\b') {
        if (has_entry && en > 0) { en--; entry[en] = 0; if (en == 0 || (en == 1 && entry[0] == '-')) { en = 0; entry[0] = 0; has_entry = 0; } }
        return;
    }
    if (c == '=') { if (op || has_entry) { compute(); has_entry = 0; en = 0; op = 0; pressed = 0; } return; }
    /* + - * / */
    if (has_entry || !op) { if (op) compute(); else acc = cur_value(); }
    if (err) return;
    has_entry = 0; en = 0; op = c; pressed = c;
}

int calc_key(i32 k)
{
    char c = 0;
    if (k >= '0' && k <= '9') c = (char)k;
    else if (k == '.' || k == ',') c = '.';
    else if (k == '+' || k == '-' || k == '*' || k == '/' || k == '%') c = (char)k;
    else if (k == 'x' || k == 'X') c = '*';
    else if (k == '=' || k == '\n' || k == '\r') c = '=';
    else if (k == '\b') c = '\b';
    else if (k == 27 || k == 'c' || k == 'C') c = 'c';
    else if (k == 'n' || k == 'N') c = 'n';
    if (!c) return 0;
    press(c);
    return 1;
}

static void btn_rect(int r, int c, i32 *x, i32 *y, i32 *w)
{
    *x = PAD + c * (BTN + GAP); *y = DISP_H + r * (BTN + GAP); *w = BTN;
    if (r == 4 && c == 0) *w = 2 * BTN + GAP;
}

int calc_click(i32 x, i32 y)
{
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 4; c++) {
            if (!KEYS[r][c].code) continue;
            i32 bx, by, bw; btn_rect(r, c, &bx, &by, &bw);
            if (x >= bx && x < bx + bw && y >= by && y < by + BTN) { press(KEYS[r][c].code); return 1; }
        }
    return 0;
}

void calc_draw(i32 x, i32 y, i32 w, i32 h)
{
    (void)w;
    fb_fill_rect(x, y, CALC_W, h, RGB(28, 28, 32));
    char buf[40];
    if (err) { buf[0] = 'E'; buf[1] = 'r'; buf[2] = 'r'; buf[3] = 'o'; buf[4] = 'r'; buf[5] = 0; }
    else if (has_entry) { int p = 0; for (int i = 0; i < en; i++) buf[p++] = entry[i]; buf[p] = 0; if (!p) { buf[0] = '0'; buf[1] = 0; } }
    else format(acc, buf);
    if (!buf[0]) { buf[0] = '0'; buf[1] = 0; }
    int fnt = FONT_TITLE;
    i32 tw = font_width(buf, fnt);
    if (tw > CALC_W - 2 * PAD) { fnt = FONT_BOLD; tw = font_width(buf, fnt); }
    font_draw(x + CALC_W - PAD - tw, y + DISP_H - 46, buf, RGB(255, 255, 255), fnt);
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 4; c++) {
            const key_t_ *k = &KEYS[r][c];
            if (!k->code) continue;
            i32 bx, by, bw; btn_rect(r, c, &bx, &by, &bw);
            int on = k->kind == 2 && k->code == pressed;
            u32 bg = k->kind == 2 ? (on ? RGB(255, 255, 255) : RGB(255, 159, 10)) : k->kind == 1 ? RGB(165, 165, 170) : RGB(80, 80, 86);
            u32 fg = k->kind == 1 ? RGB(0, 0, 0) : (on ? RGB(255, 159, 10) : RGB(255, 255, 255));
            fb_fill_round_rect(x + bx, y + by, bw, BTN, BTN / 2, bg, 255);
            i32 lw = font_width(k->lab, FONT_TITLE);
            if (r == 4 && c == 0) font_draw(x + bx + 22, y + by + 14, k->lab, fg, FONT_TITLE);
            else font_draw(x + bx + (bw - lw) / 2, y + by + 14, k->lab, fg, FONT_TITLE);
        }
}
