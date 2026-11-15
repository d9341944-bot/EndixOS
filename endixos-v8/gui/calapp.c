/* Calendar: месяц на странице, листание стрелками, возврат к сегодняшнему дню. */
#include "calapp.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "ui.h"
#include "../kernel/event.h"

#define HEAD 56
#define PADX 14

static u32 ty = 2026, tm = 1, td = 1;           /* сегодня */
static u32 vy = 2026, vm = 1;                   /* показанный месяц */
static int inited;

static const char *const mon[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static const char *const dn[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

static int leap(u32 y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int mdays(u32 y, u32 m) { static const u8 d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; return m == 2 && leap(y) ? 29 : d[m - 1]; }
static int wday(u32 y, u32 m, u32 d)             /* 0 = понедельник */
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int yy = (int)y - (m < 3);
    int w = (yy + yy / 4 - yy / 100 + yy / 400 + t[m - 1] + (int)d) % 7;    /* 0 = воскресенье */
    return (w + 6) % 7;
}

void cal_set_today(u32 y, u32 m, u32 d)
{
    if (m < 1 || m > 12 || d < 1) return;
    int first = !inited || (y != ty || m != tm);
    ty = y; tm = m; td = d;
    if (first) { vy = y; vm = m; }
    inited = 1;
}

static void shift(int dm)
{
    int m = (int)vm + dm, y = (int)vy;
    while (m < 1) { m += 12; y--; }
    while (m > 12) { m -= 12; y++; }
    if (y < 1900) y = 1900;
    if (y > 2200) y = 2200;
    vy = (u32)y; vm = (u32)m;
}

int cal_key(i32 k)
{
    if (k == KEY_LEFT) { shift(-1); return 1; }
    if (k == KEY_RIGHT) { shift(1); return 1; }
    if (k == KEY_UP) { shift(-12); return 1; }
    if (k == KEY_DOWN) { shift(12); return 1; }
    if (k == 't' || k == 'T' || k == KEY_HOME) { vy = ty; vm = tm; return 1; }
    return 0;
}

int cal_click(i32 x, i32 y)
{
    if (y < HEAD - 8 && y >= 10) {
        if (x < 52) { shift(-1); return 1; }
        if (x >= CAL_W - 52 && x < CAL_W - 8) { shift(1); return 1; }
        if (x >= CAL_W - 112 && x < CAL_W - 56) { vy = ty; vm = tm; return 1; }
    }
    return 0;
}

void cal_draw(i32 x, i32 y, i32 w, i32 h)
{
    const theme_t *t = theme();
    fb_fill_rect(x, y, w, h, t->body);
    char b[32], *p = b;
    for (const char *s = mon[vm - 1]; *s;) *p++ = *s++;
    *p++ = ' ';
    { char d[6]; int n = 0; u32 v = vy; while (v) { d[n++] = (char)('0' + v % 10); v /= 10; } while (n) *p++ = d[--n]; }
    *p = 0;
    font_draw(x + PADX, y + 14, b, t->text, FONT_TITLE);
    ui_chevron(x + w - 38, y + 24, -1, t->text);
    ui_chevron(x + w - 16, y + 24, 1, t->text);
    ui_button(x + w - 112, y + 10, 52, 24, "Today", 1, 0);
    i32 cw = (w - 2 * PADX) / 7, ch = (h - HEAD - 24) / 6;
    if (ch > 44) ch = 44;
    for (int i = 0; i < 7; i++)
        font_draw(x + PADX + i * cw + (cw - font_width(dn[i], FONT_REGULAR)) / 2, y + HEAD, dn[i], i >= 5 ? t->sub : t->text, FONT_BOLD);
    int first = wday(vy, vm, 1), nd = mdays(vy, vm);
    for (int d = 1; d <= nd; d++) {
        int idx = first + d - 1, col = idx % 7, row = idx / 7;
        i32 cx = x + PADX + col * cw, cy = y + HEAD + 24 + row * ch;
        char n[4], *q = n; if (d >= 10) *q++ = (char)('0' + d / 10); *q++ = (char)('0' + d % 10); *q = 0;
        int today = vy == ty && vm == tm && (u32)d == td;
        i32 tw = font_width(n, FONT_REGULAR);
        if (today) {
            fb_fill_circle(cx + cw / 2, cy + ch / 2 - 2, 15, theme_accent());
            font_draw(cx + (cw - tw) / 2, cy + ch / 2 - 10, n, RGB(255, 255, 255), FONT_BOLD);
        } else font_draw(cx + (cw - tw) / 2, cy + ch / 2 - 10, n, col >= 5 ? t->sub : t->text, FONT_REGULAR);
    }
}
