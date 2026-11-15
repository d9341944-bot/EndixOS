/* Окно просмотра картинок: вписывает изображение в окно, листает соседние файлы папки. */
#include "viewer.h"
#include "img.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "../kernel/event.h"
#include "../kernel/fs.h"

#define HEAD 30
#define FITMAX (1280 * 1024)
#define ARROW_W 40

static u32 fit[FITMAX];
static int node = -1, iw, ih, fmt, bad;
static int dw, dh, have_w, have_h;           /* размер кэшированного изображения, область, для которой оно получено */
static char title[FS_NAME_MAX + 1];
static char info[48];

static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int viewer_is_image(int n)
{
    if (!fs_valid(n) || fs_is_dir(n)) return 0;
    const char *s = fs_name(n); int l = 0;
    while (s[l]) l++;
    int d = l; while (d > 0 && s[d - 1] != '.') d--;
    if (d <= 0) return 0;
    const char *e = s + d; int el = l - d;
    if (el == 3) return (lc(e[0]) == 'p' && lc(e[1]) == 'n' && lc(e[2]) == 'g') || (lc(e[0]) == 'j' && lc(e[1]) == 'p' && lc(e[2]) == 'g') || (lc(e[0]) == 'g' && lc(e[1]) == 'i' && lc(e[2]) == 'f');
    if (el == 4) return lc(e[0]) == 'j' && lc(e[1]) == 'p' && lc(e[2]) == 'e' && lc(e[3]) == 'g';
    return 0;
}

static char *num(char *p, u32 v)
{
    char t[11]; int n = 0;
    if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) *p++ = t[--n];
    *p = 0; return p;
}

static void set_info(void)
{
    char *p = num(info, (u32)iw);
    *p++ = ' '; *p++ = 'x'; *p++ = ' ';
    num(p, (u32)ih);
}

int viewer_open(int n)
{
    if (!viewer_is_image(n)) return -1;
    node = n; bad = 0; dw = dh = 0; have_w = have_h = 0; iw = ih = 0;
    const char *s = fs_name(n); int k = 0;
    while (s[k] && k < FS_NAME_MAX) { title[k] = s[k]; k++; }
    title[k] = 0;
    fmt = img_info(fs_data(n), fs_size(n), &iw, &ih);
    if (!fmt || iw < 1 || ih < 1 || (u32)iw * (u32)ih > IMG_MAX_PIXELS) bad = 1; else set_info();
    return 0;
}

const char *viewer_title(void) { return title; }

static void step(int dir)                    /* соседняя картинка в той же папке */
{
    if (node < 0 || !fs_valid(node)) return;
    int list[FS_MAX_NODES], n = fs_list(fs_parent(node), list, FS_MAX_NODES), m = 0, pos = -1;
    for (int i = 0; i < n; i++)
        if (viewer_is_image(list[i])) { if (list[i] == node) pos = m; list[m++] = list[i]; }
    if (m < 2 || pos < 0) return;
    viewer_open(list[(pos + dir + m) % m]);
}

static int count_images(int *pos)
{
    int list[FS_MAX_NODES], m = 0;
    if (node < 0 || !fs_valid(node)) return 0;
    int n = fs_list(fs_parent(node), list, FS_MAX_NODES);
    *pos = 0;
    for (int i = 0; i < n; i++)
        if (viewer_is_image(list[i])) { if (list[i] == node) *pos = m; m++; }
    return m;
}

static void fit_size(int aw, int ah)
{
    if (iw <= aw && ih <= ah) { dw = iw; dh = ih; return; }
    if ((u32)iw * (u32)ah > (u32)ih * (u32)aw) { dw = aw; dh = (int)((u32)ih * (u32)aw / (u32)iw); }
    else { dh = ah; dw = (int)((u32)iw * (u32)ah / (u32)ih); }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
}

static void ensure_decoded(int aw, int ah)
{
    if (bad || !fs_valid(node)) { bad = 1; return; }
    if (have_w == aw && have_h == ah && dw) return;
    have_w = aw; have_h = ah;
    if (aw < 8 || ah < 8) { dw = 0; return; }
    if (aw > 1280) aw = 1280;
    if (ah > 1024) ah = 1024;
    fit_size(aw, ah);
    if (dw * dh > FITMAX || !img_decode(fs_data(node), fs_size(node), dw, dh, fit)) { bad = 1; dw = 0; }
}

void viewer_draw(i32 x, i32 y, i32 w, i32 h)
{
    const theme_t *t = theme();
    int dark = theme_is_dark();
    u32 bg = dark ? RGB(22, 22, 25) : RGB(236, 236, 240);
    fb_fill_rect(x, y, w, h, bg);
    fb_fill_rect(x, y, w, HEAD, dark ? RGB(48, 49, 54) : RGB(246, 246, 248));
    font_draw(x + 14, y + 7, title, t->text, FONT_BOLD);
    int pos, m = count_images(&pos);
    char st[64]; char *p = st;
    if (!bad && iw > 0) { const char *q = info; while (*q) *p++ = *q++; }
    if (m > 1) { *p++ = ' '; *p++ = ' '; p = num(p, (u32)pos + 1); *p++ = '/'; p = num(p, (u32)m); }
    *p = 0;
    font_draw(x + w - 14 - font_width(st, FONT_REGULAR), y + 8, st, t->sub, FONT_REGULAR);
    i32 ay = y + HEAD, ah = h - HEAD;
    ensure_decoded(w, ah);
    if (bad || !dw) {
        const char *s = bad ? "Can't open this image" : "";
        font_draw(x + (w - font_width(s, FONT_REGULAR)) / 2, ay + ah / 2 - 8, s, t->sub, FONT_REGULAR);
    } else {
        fb_blit(x + (w - dw) / 2, ay + (ah - dh) / 2, fit, dw, dh);
    }
    if (m > 1) {                                                    /* стрелки по краям */
        i32 cy = ay + ah / 2;
        fb_fill_round_rect(x + 8, cy - 18, 28, 36, 10, RGB(30, 30, 34), 150);
        fb_fill_round_rect(x + w - 36, cy - 18, 28, 36, 10, RGB(30, 30, 34), 150);
        font_draw(x + 17, cy - 9, "<", RGB(255, 255, 255), FONT_BOLD);
        font_draw(x + w - 27, cy - 9, ">", RGB(255, 255, 255), FONT_BOLD);
    }
}

int viewer_key(i32 key)
{
    if (key == KEY_LEFT || key == KEY_UP) { step(-1); return 1; }
    if (key == KEY_RIGHT || key == KEY_DOWN || key == ' ') { step(1); return 1; }
    return 0;
}

int viewer_click(i32 x, i32 y, i32 w)
{
    if (y < HEAD) return 0;
    int pos, m = count_images(&pos);
    if (m < 2) return 0;
    if (x < ARROW_W + 8) { step(-1); return 1; }
    if (x > w - ARROW_W - 8) { step(1); return 1; }
    return 0;
}
