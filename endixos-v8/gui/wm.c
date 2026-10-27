/* Оконный менеджер EndixOS: menu bar, окна, Dock, курсор, встроенные приложения. */
#include "wm.h"
#include "fb.h"
#include "genie.h"
#include "anim.h"
#include "mission.h"
#include "font.h"
#include "theme.h"
#include "control.h"
#include "logo.h"
#include "icons.h"
#include "settings.h"
#include "wallpaper.h"
#include "snake.h"
#include "files.h"
#include "desktop.h"
#include "widgets.h"
#include "../kernel/fs.h"
#include "../kernel/net.h"
#include "../kernel/string.h"

#define MENUBAR_H 24
#define TITLE_H   28
#define WIN_R     10

enum { APP_FILES, APP_TERMINAL, APP_SETTINGS, APP_SNAKE, APP_ABOUT, APP_COUNT };

typedef struct {
    int open;
    int minimized;               /* свёрнуто в Dock: не рисуется и не получает ввод */
    int maximized;               /* развёрнуто на весь экран */
    i32 x, y, w, h;
    i32 sx, sy, sw, sh;          /* позиция и размер до разворачивания */
} window_t;

static const char *const app_name[APP_COUNT]  = { "Files", "Terminal", "Settings", "Snake", "About" };
static const char *const app_title[APP_COUNT] = { "Files", "Terminal", "Settings", "Snake", "" };
static const i32 app_w[APP_COUNT] = { FILES_W, 580, 640, 380, 280 };
static const u8  app_resizable[APP_COUNT] = { 1, 1, 1, 0, 0 };
static const i32 app_minw[APP_COUNT] = { 560, 320, 640, 0, 0 };
static const i32 app_minh[APP_COUNT] = { 260 + TITLE_H, 150 + TITLE_H, 520, 0, 0 };
static const i32 app_h[APP_COUNT] = { FILES_H + TITLE_H, 360, 520, 400, 520 };

static void open_window(int app);
static int lp_open;
static void genie_finish(void);
static void mc_toggle(void);
static void lp_close(void);
static void lp_show(void);
static void notify(const char *title, const char *text);
static int  sp_on;
static void sp_toggle(void);
static void mc_toggle(void);
static void close_window(int app);
static void shutdown_now(void);
static void menu_close(void);
static void genie_begin(int app, int dir);
static void draw_window(int app, int focused);

/* ---------- состояние ---------- */

static sysinfo_t sys;
static window_t  wins[APP_COUNT];
static int       zorder[APP_COUNT];         /* порядок снизу вверх */
static int       nz;
static i32       cur_x, cur_y;
static u8        btn_prev;
static int       drag_app = -1;
static i32       drag_dx, drag_dy;
static u32       uptime_s;
static u32       t_hour, t_min, t_day, t_mon, t_year;
static int       have_time;
static int       dock_magnify = 1;          /* опции из Settings */
static int       clock24 = 1;
static int       reduce_transp;
static int       mc_on;                     /* Mission Control открыт (цель) */
static i32       mc_v;                      /* 0..1024, плавно */
static int       mc_n, mc_ids[APP_COUNT];   /* окна в обзоре */
static i32       mc_x[APP_COUNT], mc_y[APP_COUNT], mc_w[APP_COUNT], mc_h[APP_COUNT];   /* куда раскладываются */
static int       mc_hover = -1;
static int       dock_hide;                 /* 0 всегда виден, 1 автоскрытие, 2 умное (прячется, если мешает окну) */
static i32       dock_hide_v;               /* 0 показан .. 1024 спрятан, плавно */
static i32       dock_off;                  /* на сколько пикселей Dock сдвинут вниз */
static u32       dock_hold;                 /* до какого тика Dock не прячем после ухода курсора */
static int       dock_zoom = 50;            /* 0..100: размер значка под курсором */
static int       gn_app = -1, gn_dir;       /* "джинн": окно и направление (1 сворачиваем, -1 разворачиваем) */
static u32       gn_t0;
#define GZ_DUR 15                           /* появление и исчезновение окна, тиков */
static int       booting = 1;               /* стартовые окна без анимации */
static i32       mn_v, cc_v, lp_v;          /* плавность меню, Control Center, Launchpad: 0..1024 */
static int       mn_last;                   /* меню, которое рисуется, пока гаснет */
static i32       dk_mag_v, dk_cx16;         /* плавное увеличение Dock и сглаженный x курсора (1/16 пикселя) */
static u32       an_last;
static int       ma_app = -1;               /* окно, которое плавно разворачивается или возвращается */
static i32       ma_f[4], ma_t[4];          /* x,y,w,h: откуда и куда */
static u32       ma_t0;
#define MA_DUR 18
static int       bn_app = -1;               /* значок, который подпрыгивает при запуске */
static u32       bn_t0;
#define BN_HOP 32                           /* тиков на один прыжок */
#define BN_DUR (BN_HOP * 3)                 /* три прыжка, каждый ниже предыдущего */
#define GN_DUR 55                           /* тиков PIT (0.55 с) */
static int       scene_dirty = 1;           /* сцену нужно перерисовать */
static int       cursor_moved;              /* изменилась только позиция курсора */
static i32       drawn_x, drawn_y;          /* где курсор нарисован в кадре */
static i32       drawn_w = 12, drawn_h = 17;
static u32       last_scene_tick;
static int       last_title_app = -1;      /* для двойного клика по заголовку */
static u32       last_title_tick;
static u32       now_ticks;               /* последнее значение pit_ticks() из главного цикла */
static int       cwd;                       /* текущая папка терминала */

/* ---------- мелкие строковые помощники ---------- */

static int s_len(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int s_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static char *s_put(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

static char *u_put(char *p, u32 v)
{
    char tmp[11];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) *p++ = tmp[--n];
    *p = 0;
    return p;
}

static char *u2_put(char *p, u32 v)
{
    *p++ = (char)('0' + (v / 10) % 10);
    *p++ = (char)('0' + v % 10);
    *p = 0;
    return p;
}

static const char *const day_names[7]  = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const mon_names[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static int day_of_week(u32 y, u32 m, u32 d)          /* 0 = воскресенье */
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    return (int)((y + y / 4 - y / 100 + y / 400 + (u32)t[m - 1] + d) % 7);
}

static void format_clock(char *out, int with_year)
{
    if (!have_time) {
        s_put(out, "--:--");
        return;
    }
    char *p = out;
    u32 m = t_mon < 1 ? 1 : (t_mon > 12 ? 12 : t_mon);
    p = s_put(p, day_names[day_of_week(t_year, m, t_day)]);
    *p++ = ' ';
    p = u_put(p, t_day);
    *p++ = ' ';
    p = s_put(p, mon_names[m - 1]);
    if (with_year) {
        *p++ = ' ';
        p = u_put(p, t_year);
    }
    p = s_put(p, "  ");
    if (clock24) {
        p = u2_put(p, t_hour);
    } else {
        u32 h12 = t_hour % 12;
        p = u_put(p, h12 ? h12 : 12);
    }
    *p++ = ':';
    p = u2_put(p, t_min);
    if (!clock24)
        p = s_put(p, t_hour >= 12 ? " PM" : " AM");
    *p = 0;
}

/* ---------- терминал ---------- */

#define TCOLS 160                 /* размер буфера; видимая часть зависит от окна */
#define TROWS 64
#define TLINE 120
#define RING_N 300                /* строк прокрутки назад */

enum { TC_DEF, TC_GREEN, TC_BLUE, TC_RED, TC_YELLOW, TC_ACCENT, TC_CYAN, TC_GRAY, TC_WHITE, TC_COUNT };

static char tbuf[TROWS][TCOLS];
static int  tcols = 80, trows = 20;     /* видимые столбцы и строки */
static u8   tab_[TROWS][TCOLS];        /* цвет каждой клетки (TC_*) */
static u8   tattr;                     /* цвет, которым пишем сейчас */
static int  trow, tcol;
static char tline[TLINE + 1];
static int  tlen, tpos;                /* длина введённой строки и положение курсора в ней */
static int  tsr, tsc;                  /* где на экране начинается ввод */
static int  tview;                     /* на сколько строк экран прокручен назад */
static char ring_ch[RING_N][TCOLS];
static u8   ring_at[RING_N][TCOLS];
static int  ring_head, ring_n;
static char capA[4096], capB[4096];    /* вывод, перехваченный для | и > */
static char *tcap;
static int  tcapn;
static const char *tin;                /* вход фильтра после | */
static int  tinn;

static void term_scroll(void)
{
    memcpy(ring_ch[ring_head], tbuf[0], TCOLS);              /* верхняя строка уходит в историю прокрутки */
    memcpy(ring_at[ring_head], tab_[0], TCOLS);
    ring_head = (ring_head + 1) % RING_N;
    if (ring_n < RING_N) ring_n++;
    memmove(&tbuf[0][0], &tbuf[1][0], (trows - 1) * TCOLS);
    memmove(&tab_[0][0], &tab_[1][0], (trows - 1) * TCOLS);
    memset(&tbuf[trows - 1][0], ' ', TCOLS);
    memset(&tab_[trows - 1][0], 0, TCOLS);
    if (tsr > 0) tsr--;
}

static void term_newline(void)
{
    tcol = 0;
    if (++trow >= trows) {
        term_scroll();
        trow = trows - 1;
    }
}

static void term_putc(char c)
{
    if (tcap) {
        if (tcapn < 4095) tcap[tcapn++] = c;
        return;
    }
    if (c == '\n') {
        term_newline();
        return;
    }
    if (tcol >= tcols)
        term_newline();
    tab_[trow][tcol] = tattr;
    tbuf[trow][tcol++] = c;
}

static void term_puts(const char *s)
{
    while (*s) term_putc(*s++);
}

static void term_prompt(void)
{
    char path[96], home[48], out[128];

    if (!fs_is_dir(cwd))                    /* папку удалили из Files */
        cwd = fs_home();
    fs_path(cwd, path, sizeof(path));
    fs_path(fs_home(), home, sizeof(home));
    int hl = s_len(home);
    int under_home = 1;
    for (int i = 0; i < hl; i++)
        if (path[i] != home[i])
            under_home = 0;
    char *p = out;
    if (under_home && (path[hl] == 0 || path[hl] == '/')) {
        *p++ = '~';
        p = s_put(p, path + hl);
    } else {
        p = s_put(p, path);
    }
    tattr = TC_GREEN;
    term_puts("endix@v8");
    tattr = TC_DEF;
    term_putc(' ');
    tattr = TC_BLUE;
    term_puts(out);
    tattr = TC_DEF;
    term_puts(" % ");
    tsr = trow;                             /* отсюда начинается ввод */
    tsc = tcol;
    tlen = tpos = 0;
    tline[0] = 0;
}

static void term_clear(void)
{
    memset(tbuf, ' ', sizeof(tbuf));
    memset(tab_, 0, sizeof(tab_));
    trow = tcol = 0;
    tsr = tsc = 0;
    tview = 0;
    ring_n = 0;
}

static void cm_resized(int old_cols);

/* Окно терминала изменило размер: пересчитываем видимые столбцы и строки. */
static void term_resize(i32 cw, i32 ch)
{
    int nc = (cw - 20) / FONT_MONO_ADV, nr = (ch - 16) / 15;
    if (nc < 20) nc = 20;
    if (nc > TCOLS) nc = TCOLS;
    if (nr < 5) nr = 5;
    if (nr > TROWS) nr = TROWS;
    if (nc == tcols && nr == trows)
        return;
    while (trow >= nr) {                         /* строк стало меньше: текст уезжает вверх */
        memcpy(ring_ch[ring_head], tbuf[0], TCOLS);
        memcpy(ring_at[ring_head], tab_[0], TCOLS);
        ring_head = (ring_head + 1) % RING_N;
        if (ring_n < RING_N) ring_n++;
        memmove(&tbuf[0][0], &tbuf[1][0], (TROWS - 1) * TCOLS);
        memmove(&tab_[0][0], &tab_[1][0], (TROWS - 1) * TCOLS);
        memset(&tbuf[TROWS - 1][0], ' ', TCOLS);
        memset(&tab_[TROWS - 1][0], 0, TCOLS);
        trow--;
        if (tsr > 0) tsr--;
    }
    for (int r = trows; r < nr; r++) {           /* новое место чистое */
        memset(&tbuf[r][0], ' ', TCOLS);
        memset(&tab_[r][0], 0, TCOLS);
    }
    for (int r = 0; r < nr; r++)
        for (int c = tcols; c < nc; c++) {
            tbuf[r][c] = ' ';
            tab_[r][c] = 0;
        }
    int oc = tcols;
    tcols = nc;
    trows = nr;
    if (tcol >= tcols)
        tcol = tcols - 1;
    cm_resized(oc);
}

/* ---------- cmatrix: падающие символы в терминале ---------- */

static int  cm_on;
static u32  cm_last, cm_rng = 2463534242u;
static i16  cm_head[TCOLS];
static u8   cm_len[TCOLS], cm_spd[TCOLS], cm_cnt[TCOLS];
static char cm_ch[TROWS][TCOLS];

static u32 cm_rand(void)
{
    cm_rng ^= cm_rng << 13;                      /* xorshift32 */
    cm_rng ^= cm_rng >> 17;
    cm_rng ^= cm_rng << 5;
    return cm_rng;
}

static char cm_glyph(void)
{
    static const char set[] = "01ABCDEFGHIJKLMNOPQRSTUVWXYZ+-*<>=:;#$%&@";
    return set[cm_rand() % (sizeof(set) - 1)];
}

static void cm_column(int c, int fresh)
{
    cm_head[c] = (i16)(fresh ? -(i32)(cm_rand() % trows) : -(i32)(cm_rand() % 8));
    cm_len[c] = (u8)(5 + cm_rand() % 12);
    cm_spd[c] = (u8)(1 + cm_rand() % 3);
    cm_cnt[c] = 0;
}

static void cm_resized(int old_cols)
{
    for (int c = old_cols; c < tcols; c++)
        cm_column(c, 1);
    for (int r = 0; r < trows; r++)
        for (int c = old_cols; c < tcols; c++)
            cm_ch[r][c] = cm_glyph();
}

static void cm_start(void)
{
    cm_rng ^= now_ticks * 2654435761u;
    if (!cm_rng) cm_rng = 1;
    for (int c = 0; c < tcols; c++)
        cm_column(c, 1);
    for (int r = 0; r < trows; r++)
        for (int c = 0; c < tcols; c++)
            cm_ch[r][c] = cm_glyph();
    cm_on = 1;
}

static void cm_step(void)
{
    for (int c = 0; c < tcols; c++) {
        if (++cm_cnt[c] < cm_spd[c])
            continue;
        cm_cnt[c] = 0;
        cm_head[c]++;
        if (cm_head[c] >= 0 && cm_head[c] < trows)
            cm_ch[cm_head[c]][c] = cm_glyph();
        if (cm_head[c] - cm_len[c] > trows)
            cm_column(c, 0);
    }
    for (int k = 0; k < 12; k++)                 /* мерцание: часть символов меняется на месте */
        cm_ch[cm_rand() % trows][cm_rand() % tcols] = cm_glyph();
}

static void cm_draw(i32 ox, i32 oy, i32 w, i32 h)
{
    fb_fill_rect(ox, oy, w, h, RGB(0, 0, 0));
    i32 x0 = ox + 10, y0 = oy + 8;
    char ch[2] = { 0, 0 };
    for (int c = 0; c < tcols; c++)
        for (int r = 0; r < trows; r++) {
            i32 d = cm_head[c] - r;
            if (d < 0 || d >= cm_len[c])
                continue;
            u32 col;
            if (d == 0)
                col = RGB(220, 255, 225);
            else {
                u32 v = 255 - (u32)d * 215 / cm_len[c];
                col = RGB(0, v, v / 6);
            }
            ch[0] = cm_ch[r][c];
            font_draw(x0 + c * FONT_MONO_ADV, y0 + r * 15, ch, col, FONT_MONO);
        }
    font_draw(ox + w - 150, oy + h - 20, "q / Ctrl+C - exit", RGB(0, 110, 30), FONT_MONO);
}

/* ---------- файловые команды терминала ---------- */

static char cpbuf[FS_FILE_MAX];             /* буфер для cp: данные нельзя копировать прямо из пула */

/* Следующее слово строки; завершает его нулём. 0, если слов больше нет. */
static char *next_tok(char **pp)
{
    char *p = *pp;
    while (*p == ' ')
        p++;
    if (!*p) {
        *pp = p;
        return 0;
    }
    char *start = p;
    while (*p && *p != ' ')
        p++;
    if (*p)
        *p++ = 0;
    *pp = p;
    return start;
}

static const char *fs_err(int rc)
{
    switch (rc) {
    case FS_EEXIST:    return "already exists";
    case FS_EINVAL:    return "invalid name or not a directory";
    case FS_ENOSPC:    return "no space left";
    case FS_ENOTEMPTY: return "directory not empty";
    case FS_EPERM:     return "operation not permitted";
    default:           return "no such file or directory";
    }
}

static void term_err(const char *cmd, const char *what, int rc)
{
    term_puts(cmd);
    term_puts(": ");
    if (what) {
        term_puts(what);
        term_puts(": ");
    }
    term_puts(fs_err(rc));
    term_putc('\n');
}

/* Делит путь на папку и имя последнего элемента: "a/b/c" -> папка a/b, имя c. */
static int split_path(const char *path, int *parent, char *name)
{
    int len = s_len(path);
    while (len > 1 && path[len - 1] == '/')
        len--;

    int slash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (path[i] == '/') {
            slash = i;
            break;
        }
    }
    int nstart = slash + 1, nlen = len - nstart;
    if (nlen <= 0 || nlen >= FS_NAME_MAX)
        return FS_EINVAL;
    for (int i = 0; i < nlen; i++)
        name[i] = path[nstart + i];
    name[nlen] = 0;

    if (slash < 0) {
        *parent = cwd;
        return 0;
    }
    char dir[96];
    int dl = slash == 0 ? 1 : slash;         /* "/name" -> папка "/" */
    if (dl >= (int)sizeof(dir))
        return FS_EINVAL;
    for (int i = 0; i < dl; i++)
        dir[i] = path[i];
    dir[dl] = 0;
    int p = fs_resolve(cwd, dir);
    if (p < 0)
        return FS_ENOENT;
    if (!fs_is_dir(p))
        return FS_EINVAL;
    *parent = p;
    return 0;
}

/* echo text > file и echo text >> file */
static void term_write_file(const char *path, const char *text, int append)
{
    char name[FS_NAME_MAX], data[TLINE + 2];
    int parent, rc = split_path(path, &parent, name);
    if (rc < 0) {
        term_err("echo", path, rc);
        return;
    }
    int node = fs_lookup(parent, name);
    if (node < 0)
        node = fs_create(parent, name);
    if (node < 0) {
        term_err("echo", path, node);
        return;
    }
    if (fs_is_dir(node)) {
        term_puts("echo: ");
        term_puts(path);
        term_puts(": is a directory\n");
        return;
    }
    int n = s_len(text);
    memcpy(data, text, (size_t)n);
    data[n++] = '\n';
    rc = fs_write(node, data, (u32)n, append);
    if (rc < 0)
        term_err("echo", path, rc);
}

static void term_cat(const char *path)
{
    int node = fs_resolve(cwd, path);
    if (node < 0) {
        term_err("cat", path, FS_ENOENT);
        return;
    }
    if (fs_is_dir(node)) {
        term_puts("cat: ");
        term_puts(path);
        term_puts(": is a directory\n");
        return;
    }
    const u8 *d = fs_data(node);
    u32 sz = fs_size(node);
    for (u32 i = 0; i < sz; i++) {
        u8 c = d[i];
        if (c == '\n')       term_putc('\n');
        else if (c == '\t')  term_puts("  ");
        else if (c >= 32 && c < 127) term_putc((char)c);
        else if (c != '\r')  term_putc('.');
    }
    if (sz && d[sz - 1] != '\n')
        term_putc('\n');
}

static void term_ls(char *args)
{
    char *p = args, *a;
    int longfmt = 0, target = cwd;

    while ((a = next_tok(&p)) != 0) {
        if (s_eq(a, "-l")) {
            longfmt = 1;
        } else {
            target = fs_resolve(cwd, a);
            if (target < 0) {
                term_err("ls", a, FS_ENOENT);
                return;
            }
        }
    }
    if (!fs_is_dir(target)) {
        term_puts(fs_name(target));
        term_putc('\n');
        return;
    }

    int list[FS_MAX_NODES];
    int n = fs_list(target, list, FS_MAX_NODES);
    for (int i = 0; i < n; i++) {
        int dir = fs_is_dir(list[i]);
        const char *name = fs_name(list[i]);
        if (longfmt) {
            char num[16];
            if (dir) s_put(num, "<dir>");
            else     u_put(num, fs_size(list[i]));
            for (int k = s_len(num); k < 7; k++)
                term_putc(' ');
            term_puts(num);
            term_puts("  ");
            term_puts(name);
            if (dir)
                term_putc('/');
            term_putc('\n');
        } else {
            if (tcol + s_len(name) + 3 > tcols)      /* не рвём имя на границе строки */
                term_newline();
            term_puts(name);
            if (dir)
                term_putc('/');
            term_puts("  ");
        }
    }
    if (!longfmt && n > 0)
        term_putc('\n');
}

/* mv и cp: источник и приёмник; если приёмник - папка, имя сохраняется. */
static void term_mv_cp(const char *cmd, char *args)
{
    char *p = args;
    char *src = next_tok(&p), *dst = next_tok(&p);
    char name[FS_NAME_MAX];
    int parent, rc, is_mv = (cmd[0] == 'm');

    if (!src || !dst) {
        term_puts("usage: ");
        term_puts(cmd);
        term_puts(" source destination\n");
        return;
    }
    int node = fs_resolve(cwd, src);
    if (node < 0) {
        term_err(cmd, src, FS_ENOENT);
        return;
    }
    if (!is_mv && fs_is_dir(node)) {
        term_puts("cp: directories are not supported, use files\n");
        return;
    }

    int d = fs_resolve(cwd, dst);
    if (d >= 0 && fs_is_dir(d)) {
        parent = d;
        const char *n = fs_name(node);
        int i = 0;
        while (n[i] && i < FS_NAME_MAX - 1) {
            name[i] = n[i];
            i++;
        }
        name[i] = 0;
    } else {
        rc = split_path(dst, &parent, name);
        if (rc < 0) {
            term_err(cmd, dst, rc);
            return;
        }
    }

    if (is_mv) {
        rc = fs_move(node, parent, name);
    } else {
        u32 sz = fs_size(node);
        memcpy(cpbuf, fs_data(node), sz);
        rc = fs_create(parent, name);
        if (rc >= 0)
            rc = fs_write(rc, cpbuf, sz, 0);
    }
    if (rc < 0)
        term_err(cmd, dst, rc);
}

/* Команды работы с файлами. 1 - команда обработана. */
static int term_fs_cmd(const char *cmd, char *args)
{
    char *p = args, *a;
    char name[FS_NAME_MAX];
    int parent, rc, node;

    if (s_eq(cmd, "pwd")) {
        char path[96];
        fs_path(cwd, path, sizeof(path));
        term_puts(path);
        term_putc('\n');
        return 1;
    }

    if (s_eq(cmd, "cd")) {
        a = next_tok(&p);
        node = a ? fs_resolve(cwd, a) : fs_home();
        if (node < 0) {
            term_err("cd", a, FS_ENOENT);
        } else if (!fs_is_dir(node)) {
            term_puts("cd: not a directory: ");
            term_puts(a);
            term_putc('\n');
        } else {
            cwd = node;
        }
        return 1;
    }

    if (s_eq(cmd, "ls")) {
        term_ls(args);
        return 1;
    }

    if (s_eq(cmd, "cat")) {
        if (!(a = next_tok(&p))) {
            term_puts("usage: cat file\n");
            return 1;
        }
        do {
            term_cat(a);
        } while ((a = next_tok(&p)) != 0);
        return 1;
    }

    if (s_eq(cmd, "mkdir") || s_eq(cmd, "touch")) {
        int is_mkdir = (cmd[0] == 'm');
        if (!(a = next_tok(&p))) {
            term_puts("usage: ");
            term_puts(cmd);
            term_puts(" name\n");
            return 1;
        }
        do {
            rc = split_path(a, &parent, name);
            if (rc == 0) {
                if (is_mkdir)
                    rc = fs_mkdir(parent, name);
                else if (fs_lookup(parent, name) < 0)      /* touch существующего файла ничего не делает */
                    rc = fs_create(parent, name);
            }
            if (rc < 0)
                term_err(cmd, a, rc);
        } while ((a = next_tok(&p)) != 0);
        return 1;
    }

    if (s_eq(cmd, "rm")) {
        int recursive = 0, any = 0;
        while ((a = next_tok(&p)) != 0) {
            if (s_eq(a, "-r") || s_eq(a, "-rf")) {
                recursive = 1;
                continue;
            }
            any = 1;
            node = fs_resolve(cwd, a);
            if (node < 0) {
                term_err("rm", a, FS_ENOENT);
                continue;
            }
            if (fs_is_dir(node) && !recursive) {
                term_puts("rm: ");
                term_puts(a);
                term_puts(": is a directory (use rm -r)\n");
                continue;
            }
            rc = recursive ? fs_remove_tree(node) : fs_remove(node);
            if (rc < 0)
                term_err("rm", a, rc);
        }
        if (!any)
            term_puts("usage: rm [-r] path\n");
        return 1;
    }

    if (s_eq(cmd, "mv") || s_eq(cmd, "cp")) {
        term_mv_cp(cmd, args);
        return 1;
    }

    if (s_eq(cmd, "echo")) {
        char *gt = args;
        while (*gt && *gt != '>')
            gt++;
        if (!*gt) {
            term_puts(args);
            term_putc('\n');
            return 1;
        }
        int append = (gt[1] == '>');
        char *target = gt + (append ? 2 : 1);
        char *end = gt;
        while (end > args && end[-1] == ' ')
            end--;
        *end = 0;
        while (*target == ' ')
            target++;
        if (!*target)
            term_puts("echo: missing file name\n");
        else
            term_write_file(target, args, append);
        return 1;
    }
    return 0;
}

#include "termcmd.inc"

static void term_run_cmd(char *line)
{
    while (*line == ' ') line++;
    if (!*line)
        return;

    char *arg = line;
    while (*arg && *arg != ' ') arg++;
    if (*arg) { *arg++ = 0; while (*arg == ' ') arg++; }

    if (!fs_is_dir(cwd))                    /* текущую папку могли удалить из Files */
        cwd = fs_home();
    if (term_fs_cmd(line, arg))
        return;
    if (term_ext_cmd(line, arg))
        return;

    char buf[96];
    if (s_eq(line, "snake")) {
        open_window(APP_SNAKE);
    } else if (s_eq(line, "files")) {
        open_window(APP_FILES);
    } else if (s_eq(line, "wallpaper")) {
        int id = -1, num = 0;
        for (int i = 0; arg[i] >= '0' && arg[i] <= '9' && i < 3; i++)
            num = num * 10 + (arg[i] - '0');
        if (arg[0] >= '0' && arg[0] <= '9' && num >= 1 && num <= WALL_COUNT)
            id = num - 1;
        if (id < 0) {
            term_puts("usage: wallpaper N\n");
            for (int i = 0; i < WALL_COUNT; i++) {
                int n = i + 1;
                term_putc(n >= 10 ? (char)('0' + n / 10) : ' ');
                term_putc((char)('0' + n % 10));
                term_puts("  ");
                term_puts(wallpaper_name(i));
                term_putc('\n');
            }
        } else {
            settings_set_wallpaper(id);
            term_puts("wallpaper: ");
            term_puts(wallpaper_name(id));
            term_putc('\n');
        }
    } else if (s_eq(line, "theme")) {
        if (s_eq(arg, "dark"))        theme_set_dark(1);
        else if (s_eq(arg, "light"))  theme_set_dark(0);
        else { term_puts("usage: theme light|dark\n"); return; }
        term_puts("theme: ");
        term_puts(arg);
        term_putc('\n');
    } else if (s_eq(line, "cmatrix")) {
        cm_start();
    } else if (s_eq(line, "clear")) {
        term_clear();
    } else if (s_eq(line, "uname")) {
        if (s_eq(arg, "-a"))
            term_puts("EndixOS endix-v8 " ENDIX_VERSION " i686 own-kernel\n");
        else
            term_puts("EndixOS 8.0 i686 (own kernel, written from scratch)\n");
    } else if (s_eq(line, "uptime")) {
        char *p = s_put(buf, "up ");
        p = u_put(p, uptime_s / 60);
        p = s_put(p, " min ");
        p = u_put(p, uptime_s % 60);
        p = s_put(p, " s\n");
        term_puts(buf);
    } else if (s_eq(line, "date")) {
        format_clock(buf, 1);
        term_puts(buf);
        term_putc('\n');
    } else if (s_eq(line, "cpu")) {
        term_puts(sys.cpu);
        term_putc('\n');
    } else if (s_eq(line, "mem")) {
        char *p = s_put(buf, "RAM: ");
        p = u_put(p, sys.ram_mb);
        p = s_put(p, " MB\n");
        term_puts(buf);
    } else if (s_eq(line, "reboot")) {
        sys_reboot();
    } else {
        term_cputs("endix: command not found: ", TC_RED);
        term_puts(line);
        term_putc('\n');
    }
}

/* Строка целиком: cmd1 | cmd2 и перенаправление > / >> в файл. */
static void term_run(char *line)
{
    char *right = 0, *c;
    for (c = line; *c; c++)
        if (*c == '|') { *c = 0; right = c + 1; break; }
    char *rd = right ? right : line;
    char *target = 0;
    int append = 0;
    for (c = rd; *c; c++)
        if (*c == '>') {
            char *e = c;
            while (e > rd && e[-1] == ' ') e--;           /* пробелы перед > не часть команды */
            *e = 0;
            *c = 0;
            if (c[1] == '>') { append = 1; c++; }
            target = c + 1;
            while (*target == ' ') target++;
            int n = s_len(target);
            while (n > 0 && target[n - 1] == ' ') target[--n] = 0;
            break;
        }
    if (target && !*target) {
        term_puts("endix: missing file name after >\n");
        return;
    }
    if (right) {
        tcap = capA;                                   /* левая часть пишет в буфер */
        tcapn = 0;
        term_run_cmd(line);
        tcap = 0;
        capA[tcapn] = 0;
        tin = capA;
        tinn = tcapn;
    }
    if (target) {
        tcap = capB;
        tcapn = 0;
    }
    term_run_cmd(right ? right : line);
    tin = 0;
    if (target) {
        tcap = 0;
        capB[tcapn] = 0;
        char name[FS_NAME_MAX];
        int parent, rc = split_path(target, &parent, name);
        if (rc == 0) {
            int node = fs_lookup(parent, name);
            if (node < 0) node = fs_create(parent, name);
            if (node < 0) rc = node;
            else if (fs_is_dir(node)) { term_puts("endix: is a directory: "); term_puts(target); term_putc('\n'); return; }
            else rc = fs_write(node, capB, (u32)tcapn, append);
        }
        if (rc < 0) term_err("endix", target, rc);
    }
}

/* ---------- ввод строки: курсор, история, Tab ---------- */

#define HIST_N 24
static char thist[HIST_N][TLINE + 1];
static int  thn, thi = -1;
static char tdraft[TLINE + 1];
static int  tend_r, tend_c;                /* где экранный курсор, когда он стоит в конце строки */

static void term_hist_print(void)
{
    for (int i = 0; i < thn; i++) {
        char b[12];
        u_put(b, (u32)i + 1);
        for (int k = s_len(b); k < 4; k++) term_putc(' ');
        term_cputs(b, TC_GRAY);
        term_puts("  ");
        term_puts(thist[i]);
        term_putc('\n');
    }
}

/* Перерисовывает введённую строку; oldlen - сколько символов было до правки (хвост затираем). */
static void term_redraw(int oldlen)
{
    u8 o = tattr;
    tattr = TC_DEF;
    trow = tsr;
    tcol = tsc;
    for (int i = 0; i < tlen; i++)
        term_putc(tline[i]);
    tend_r = trow;
    tend_c = tcol;
    for (int i = tlen; i < oldlen; i++)
        term_putc(' ');
    tattr = o;
    if (tpos == tlen) {
        trow = tend_r;
        tcol = tend_c;
    } else {
        int idx = tsc + tpos, r = tsr + idx / tcols;
        trow = r >= trows ? trows - 1 : r;
        tcol = idx % tcols;
    }
}

static void term_set_line(const char *s)
{
    int old = tlen;
    tlen = 0;
    while (s[tlen] && tlen < TLINE) { tline[tlen] = s[tlen]; tlen++; }
    tline[tlen] = 0;
    tpos = tlen;
    term_redraw(old);
}

static void term_insert(const char *s, int n)
{
    int old = tlen;
    if (tlen + n > TLINE) n = TLINE - tlen;
    if (n <= 0) return;
    memmove(&tline[tpos + n], &tline[tpos], (size_t)(tlen - tpos));
    memcpy(&tline[tpos], s, (size_t)n);
    tlen += n;
    tpos += n;
    tline[tlen] = 0;
    term_redraw(old);
}

static void term_delete(int from, int to)   /* убрать символы [from, to) */
{
    if (from < 0) from = 0;
    if (to > tlen) to = tlen;
    if (to <= from) return;
    int old = tlen;
    memmove(&tline[from], &tline[to], (size_t)(tlen - to));
    tlen -= to - from;
    tline[tlen] = 0;
    tpos = from;
    term_redraw(old);
}

static void term_complete(void)
{
    int ws = tpos;
    while (ws > 0 && tline[ws - 1] != ' ') ws--;
    char pre[TLINE + 1];
    int pl = tpos - ws;
    memcpy(pre, &tline[ws], (size_t)pl);
    pre[pl] = 0;
    int first = 1;                                  /* это имя команды, а не аргумент */
    for (int i = 0; i < ws; i++) if (tline[i] != ' ') first = 0;

    static char cand[32][FS_NAME_MAX + 1];
    static u8 cdir[32];
    int nc = 0;
    int np = pl;                                    /* длина вводимой части имени */
    const char *namepart = pre;
    if (first) {
        for (int i = 0; i < CMD_COUNT && nc < 32; i++)
            if (s_starts(cmd_table[i].name, pre) && s_len(cmd_table[i].name) <= FS_NAME_MAX) {
                s_put(cand[nc], cmd_table[i].name);
                cdir[nc++] = 0;
            }
    } else {
        int slash = -1;
        for (int i = 0; i < pl; i++) if (pre[i] == '/') slash = i;
        int dir = cwd;
        if (slash >= 0) {
            char dp[TLINE + 1];
            int dl = slash == 0 ? 1 : slash;
            memcpy(dp, pre, (size_t)dl);
            dp[dl] = 0;
            dir = fs_resolve(cwd, dp);
            namepart = pre + slash + 1;
            np = pl - slash - 1;
        }
        if (dir >= 0 && fs_is_dir(dir)) {
            int list[FS_MAX_NODES];
            int n = fs_list(dir, list, FS_MAX_NODES);
            for (int i = 0; i < n && nc < 32; i++)
                if (s_starts(fs_name(list[i]), namepart)) {
                    s_put(cand[nc], fs_name(list[i]));
                    cdir[nc++] = (u8)fs_is_dir(list[i]);
                }
        }
    }
    if (nc == 0) return;
    int common = s_len(cand[0]);
    for (int i = 1; i < nc; i++) {
        int k = 0;
        while (k < common && cand[i][k] == cand[0][k]) k++;
        common = k;
    }
    if (common > np) {
        term_insert(cand[0] + np, common - np);
        if (nc == 1) term_insert(cdir[0] ? "/" : " ", 1);
        return;
    }
    if (nc == 1) {
        term_insert(cdir[0] ? "/" : " ", 1);
        return;
    }
    char save[TLINE + 1];                           /* несколько вариантов: показываем список и возвращаем строку */
    int slen = tlen, spos = tpos;
    memcpy(save, tline, (size_t)tlen + 1);
    trow = tend_r;
    tcol = tend_c;
    term_putc('\n');
    for (int i = 0; i < nc; i++) {
        if (tcol + s_len(cand[i]) + 3 > tcols) term_newline();
        term_cputs(cand[i], cdir[i] ? TC_BLUE : TC_DEF);
        if (cdir[i]) term_putc('/');
        term_puts("  ");
    }
    term_putc('\n');
    term_prompt();
    memcpy(tline, save, (size_t)slen + 1);
    tlen = slen;
    tpos = spos;
    term_redraw(0);
}

static void term_enter(void)
{
    char cmdline[TLINE + 1];
    trow = tend_r;
    tcol = tend_c;
    term_putc('\n');
    tline[tlen] = 0;
    memcpy(cmdline, tline, (size_t)tlen + 1);
    if (tlen > 0 && !(thn > 0 && s_eq(thist[thn - 1], cmdline))) {
        if (thn == HIST_N) {
            memmove(thist[0], thist[1], (size_t)(HIST_N - 1) * (TLINE + 1));
            thn--;
        }
        memcpy(thist[thn++], cmdline, (size_t)tlen + 1);
    }
    thi = -1;
    term_run(cmdline);
    tcap = 0;
    tin = 0;
    if (net_job_active()) { nj_wait = 1; return; }  /* ping/nslookup: приглашение появится по завершении */
    term_prompt();
    tend_r = trow;
    tend_c = tcol;
}

static void term_key(i32 c, i32 ctrl)
{
    if (nj_wait) {                                 /* пока идёт ping, работает только Ctrl+C */
        if (ctrl && (c == 'c' || c == 'C')) { term_cputs("^C\n", TC_GRAY); net_stop(); }
        return;
    }
    if (cm_on) {                                   /* в cmatrix клавиши только для выхода */
        if (c == 'q' || c == 'Q' || c == 27 || (ctrl && (c == 'c' || c == 'C')))
            cm_on = 0;
        return;
    }
    if (c == KEY_PGUP) { tview += trows / 2; if (tview > ring_n) tview = ring_n; return; }
    if (c == KEY_PGDN) { tview -= trows / 2; if (tview < 0) tview = 0; return; }
    tview = 0;                                     /* любая другая клавиша возвращает к вводу */

    if (ctrl && c >= 'A' && c <= 'Z') c += 32;
    if (ctrl && c >= 'a' && c <= 'z') {
        switch (c) {
        case 'l': {
            char save[TLINE + 1];
            int sl = tlen, sp = tpos;
            memcpy(save, tline, (size_t)tlen + 1);
            term_clear();
            term_prompt();
            memcpy(tline, save, (size_t)sl + 1);
            tlen = sl; tpos = sp;
            term_redraw(0);
            break;
        }
        case 'c':
            trow = tend_r; tcol = tend_c;
            term_cputs("^C", TC_GRAY);
            term_putc('\n');
            thi = -1;
            term_prompt();
            tend_r = trow; tend_c = tcol;
            break;
        case 'u': term_delete(0, tpos); break;
        case 'k': term_delete(tpos, tlen); break;
        case 'a': tpos = 0; term_redraw(tlen); break;
        case 'e': tpos = tlen; term_redraw(tlen); break;
        case 'w': {
            int e = tpos;
            while (e > 0 && tline[e - 1] == ' ') e--;
            while (e > 0 && tline[e - 1] != ' ') e--;
            term_delete(e, tpos);
            break;
        }
        default: break;
        }
        return;
    }
    switch (c) {
    case '\n': term_enter(); return;
    case '\b': if (tpos > 0) term_delete(tpos - 1, tpos); return;
    case KEY_DELETE: term_delete(tpos, tpos + 1); return;
    case KEY_LEFT:  if (tpos > 0) { tpos--; term_redraw(tlen); } return;
    case KEY_RIGHT: if (tpos < tlen) { tpos++; term_redraw(tlen); } return;
    case KEY_HOME:  tpos = 0; term_redraw(tlen); return;
    case KEY_END:   tpos = tlen; term_redraw(tlen); return;
    case KEY_UP:
        if (thn == 0) return;
        if (thi < 0) { memcpy(tdraft, tline, (size_t)tlen + 1); thi = thn; }
        if (thi > 0) { thi--; term_set_line(thist[thi]); }
        return;
    case KEY_DOWN:
        if (thi < 0) return;
        thi++;
        if (thi >= thn) { thi = -1; term_set_line(tdraft); }
        else term_set_line(thist[thi]);
        return;
    case '\t': term_complete(); return;
    default: break;
    }
    if (c >= 32 && c < 127) {
        char ch = (char)c;
        term_insert(&ch, 1);
    }
}

/* ---------- окна ---------- */

static int top_app(void)                    /* верхнее видимое окно */
{
    for (int i = nz - 1; i >= 0; i--)
        if (!wins[zorder[i]].minimized)
            return zorder[i];
    return -1;
}

static void raise_window(int app)
{
    int i;
    for (i = 0; i < nz; i++)
        if (zorder[i] == app) break;
    if (i == nz) return;
    for (; i < nz - 1; i++)
        zorder[i] = zorder[i + 1];
    zorder[nz - 1] = app;
}

static void open_window(int app)
{
    window_t *w = &wins[app];
    genie_finish();
    if (w->open) {
        if (w->minimized) {                 /* клик по иконке в Dock возвращает свёрнутое окно: "джинн" наоборот */
            genie_begin(app, -1);
            return;
        }
        raise_window(app);
        return;
    }
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    i32 off = 30 * (nz % 5);
    w->w = app_w[app];
    w->h = app_h[app];
    w->x = (W - w->w) / 2 - 60 + off;
    w->y = MENUBAR_H + 40 + off;
    if (w->x < 8) w->x = 8;
    if (w->y + w->h > H - 90) w->y = H - 90 - w->h;
    if (w->y < MENUBAR_H + 4) w->y = MENUBAR_H + 4;
    w->open = 1;
    w->minimized = w->maximized = 0;
    zorder[nz++] = app;
    if (!booting) {
        bn_app = app;                       /* значок в Dock подпрыгивает, окно плавно вырастает */
        bn_t0 = now_ticks;
        genie_begin(app, 2);
        w->minimized = 1;
    }
}

static void close_window(int app)
{
    int i;
    genie_finish();
    if (!booting && wins[app].open && !wins[app].minimized)
        genie_begin(app, 3);
    for (i = 0; i < nz; i++)
        if (zorder[i] == app) break;
    for (; i < nz - 1; i++)
        zorder[i] = zorder[i + 1];
    if (nz) nz--;
    wins[app].open = 0;
    wins[app].minimized = wins[app].maximized = 0;
}

static void minimize_window(int app)
{
    genie_finish();
    if (!wins[app].open || wins[app].minimized) return;
    genie_begin(app, 1);
    wins[app].minimized = 1;
    drag_app = -1;
}

static void max_finish(void)
{
    if (ma_app < 0) return;
    window_t *w = &wins[ma_app];
    w->x = ma_t[0]; w->y = ma_t[1]; w->w = ma_t[2]; w->h = ma_t[3];
    ma_app = -1;
    scene_dirty = 1;
}

static void toggle_maximize(int app)
{
    window_t *w = &wins[app];
    max_finish();
    i32 from[4] = { w->x, w->y, w->w, w->h };
    if (w->maximized) {
        w->x = w->sx;
        w->y = w->sy;
        w->w = w->sw;
        w->h = w->sh;
        w->maximized = 0;
    } else {
        w->sx = w->x;
        w->sy = w->y;
        w->sw = w->w;
        w->sh = w->h;
        w->x = 0;
        w->y = MENUBAR_H;
        w->w = (i32)fb_width();
        w->h = (i32)fb_height() - MENUBAR_H - (dock_hide ? 0 : 84);     /* внизу место для Dock, если он не прячется */
        w->maximized = 1;
    }
    drag_app = -1;
    if (!booting && w->open && !w->minimized) {              /* плавно переходим к новому размеру */
        ma_t[0] = w->x; ma_t[1] = w->y; ma_t[2] = w->w; ma_t[3] = w->h;
        for (int i = 0; i < 4; i++) ma_f[i] = from[i];
        ma_app = app;
        ma_t0 = now_ticks;
        w->x = from[0]; w->y = from[1]; w->w = from[2]; w->h = from[3];
    }
}

static void max_step(void)
{
    if (ma_app < 0) return;
    if (!wins[ma_app].open || wins[ma_app].minimized) { max_finish(); return; }
    i32 p = (i32)(now_ticks - ma_t0) * 1024 / MA_DUR;
    if (p >= 1024) { max_finish(); return; }
    i32 e = anim_ease_out(p);
    window_t *w = &wins[ma_app];
    w->x = ma_f[0] + (ma_t[0] - ma_f[0]) * e / 1024;
    w->y = ma_f[1] + (ma_t[1] - ma_f[1]) * e / 1024;
    w->w = ma_f[2] + (ma_t[2] - ma_f[2]) * e / 1024;
    w->h = ma_f[3] + (ma_t[3] - ma_f[3]) * e / 1024;
}

static i32 content_w(int app) { return app_resizable[app] ? wins[app].w : app_w[app]; }
static i32 content_h(int app) { return (app_resizable[app] ? wins[app].h : app_h[app]) - TITLE_H; }

/* Левый верхний угол области содержимого. У окон с изменяемым размером содержимое растягивается,
 * у остальных (Snake, About) в развёрнутом окне стоит по центру. */
static void content_origin(int app, i32 *ox, i32 *oy)
{
    const window_t *w = &wins[app];
    if (app_resizable[app]) {
        *ox = w->x;
        *oy = w->y + TITLE_H;
        return;
    }
    *ox = w->x + (w->w - app_w[app]) / 2;
    *oy = w->y + TITLE_H + (w->h - app_h[app]) / 2;
}

/* ---------- Dock ---------- */

#define DOCK_ICON 52
#define DOCK_GAP  8
#define DOCK_PAD  10
#define LP_ID      APP_COUNT            /* Launchpad в Dock: id после приложений, слот первый */
#define DOCK_SLOTS (APP_COUNT + 1)

static i32 isin_w(i32 p)                       /* период 256, -256..256 */
{
    p &= 255;
    if (p < 128) return (p * (128 - p)) >> 4;
    p -= 128;
    return -((p * (128 - p)) >> 4);
}

#define DOCK_MAX  (56 + 48 * dock_zoom / 100)           /* размер значка под курсором: 56..104 */
#define DOCK_BAND (DOCK_MAX + 30)                        /* высота зоны у нижнего края, где работает увеличение */
#define DOCK_WAVE (50 + DOCK_MAX * 1)                    /* радиус "волны" увеличения */

static i32 dk_x[DOCK_SLOTS], dk_s[DOCK_SLOTS];       /* левый край и размер значка в слоте */
static i32 dk_bx, dk_bw;                              /* плашка Dock */

/* Раскладка Dock для текущего положения курсора: значки у курсора растут, соседи раздвигаются. */
static void dock_layout(void)
{
    i32 W = (i32)fb_width();
    i32 gw0 = DOCK_SLOTS * DOCK_ICON + (DOCK_SLOTS - 1) * DOCK_GAP;
    i32 bx0 = (W - gw0) / 2;
    i32 mv = dock_magnify ? dk_mag_v : 0;             /* 0..1024, плавно растёт и спадает */
    i32 mcx = dk_cx16 >> 4;
    i32 total = 0;
    for (int k = 0; k < DOCK_SLOTS; k++) {
        i32 sz = DOCK_ICON;
        if (mv > 0) {
            i32 c = bx0 + k * (DOCK_ICON + DOCK_GAP) + DOCK_ICON / 2;
            i32 d = mcx - c;
            if (d < 0) d = -d;
            if (d < DOCK_WAVE) {
                i32 f = (isin_w(d * 128 / DOCK_WAVE + 64) + 256) / 2;      /* 256 в центре, 0 на краю */
                sz = DOCK_ICON + (DOCK_MAX - DOCK_ICON) * f / 256 * mv / 1024;
            }
        }
        dk_s[k] = sz;
        total += sz;
    }
    total += (DOCK_SLOTS - 1) * DOCK_GAP;
    i32 x = (W - total) / 2;
    dk_bx = x - DOCK_PAD;
    dk_bw = total + 2 * DOCK_PAD;
    for (int k = 0; k < DOCK_SLOTS; k++) {
        dk_x[k] = x;
        x += dk_s[k] + DOCK_GAP;
    }
}

static void dock_geom(i32 *dx, i32 *dy, i32 *dw, i32 *dh)
{
    dock_layout();
    *dw = dk_bw;
    *dh = DOCK_ICON + DOCK_PAD * 3 / 2;
    *dx = dk_bx;
    *dy = (i32)fb_height() - *dh - 8 + dock_off;
}

/* Где Dock стоит, когда виден: нужно, чтобы решить, мешает ли он окну. */
static void dock_rest_rect(i32 *x, i32 *y, i32 *w, i32 *h)
{
    i32 gw0 = DOCK_SLOTS * DOCK_ICON + (DOCK_SLOTS - 1) * DOCK_GAP;
    *w = gw0 + 2 * DOCK_PAD;
    *x = ((i32)fb_width() - *w) / 2;
    *h = DOCK_ICON + DOCK_PAD * 3 / 2;
    *y = (i32)fb_height() - *h - 8;
}

static int dock_covers_window(void)               /* какое-нибудь видимое окно заходит на Dock */
{
    i32 x, y, w, h;
    dock_rest_rect(&x, &y, &w, &h);
    for (int i = 0; i < nz; i++) {
        const window_t *k = &wins[zorder[i]];
        if (k->minimized || !k->open) continue;
        if (k->maximized || (k->x < x + w && k->x + k->w > x && k->y + k->h > y - 4))
            return 1;
    }
    return 0;
}

static void dock_icon_pos(int i, i32 *ix, i32 *iy)
{
    i32 dx, dy, dw, dh;
    dock_geom(&dx, &dy, &dw, &dh);
    int slot = i == LP_ID ? 0 : i + 1;
    *ix = dk_x[slot];
    *iy = dy + dh - DOCK_PAD * 3 / 4 - dk_s[slot];     /* значки стоят на одной линии, растут вверх */
}

static int dock_icon_center(int app, i32 *cx, i32 *cy, i32 *iw)
{
    i32 ix, iy;
    dock_icon_pos(app, &ix, &iy);
    i32 sz = dk_s[app + 1];
    *cx = ix + sz / 2;
    *cy = iy + sz / 2;
    *iw = sz - 8;
    return 1;
}

static int dock_hit(i32 x, i32 y)
{
    for (int i = 0; i <= LP_ID; i++) {
        i32 ix, iy;
        dock_icon_pos(i, &ix, &iy);
        i32 sz = dk_s[i == LP_ID ? 0 : i + 1];
        if (x >= ix && x < ix + sz && y >= iy && y < iy + sz)
            return i;
    }
    return -1;
}


/* Высота прыжка значка: три дуги, 30 -> 18 -> 8 пикселей, с мягким приземлением. */
static i32 bounce_height(void)
{
    if (bn_app < 0) return 0;
    i32 t = (i32)(now_ticks - bn_t0);
    if (t < 0 || t >= BN_DUR) return 0;
    i32 hop = t / BN_HOP, u = (t % BN_HOP) * 256 / BN_HOP;      /* u: 0..255 внутри прыжка */
    static const i32 hh[3] = { 30, 18, 8 };
    return hh[hop] * (4 * u * (256 - u) / 256) / 256;
}

static void draw_dock(void)
{
    i32 dx, dy, dw, dh;
    dock_geom(&dx, &dy, &dw, &dh);
    if (dy >= (i32)fb_height()) return;
    const theme_t *t = theme();
    fb_glass(dx, dy, dw, dh, 22, GLASS_ALL, 10, t->dock_tint, theme_is_dark() ? 105 : 90, 40);

    int hover = dock_hit(cur_x, cur_y);
    for (int i = 0; i <= LP_ID; i++) {
        i32 ix, iy;
        dock_icon_pos(i, &ix, &iy);
        int slot = i == LP_ID ? 0 : i + 1;
        i32 sz = dk_s[slot];
        fb_blend_rect(ix + 4, iy + sz - 2, sz - 8, 3, RGB(0, 0, 0), 28);          /* лёгкая тень под значком */
        icon_draw(i, ix, iy - (i == bn_app ? bounce_height() : 0), sz);
        if (i < APP_COUNT && wins[i].open)
            fb_fill_circle(ix + sz / 2, dy + dh - 5, 2, theme_is_dark() ? RGB(235, 235, 240) : RGB(50, 50, 56));
    }
    if (hover >= 0) {                                /* подпись над значком, как в macOS */
        i32 ix, iy;
        dock_icon_pos(hover, &ix, &iy);
        i32 sz = dk_s[hover == LP_ID ? 0 : hover + 1];
        const char *nm = hover == LP_ID ? "Launchpad" : app_name[hover];
        i32 tw = font_width(nm, FONT_REGULAR) + 22;
        i32 tx = ix + sz / 2 - tw / 2, ty = iy - 36;
        fb_glass(tx, ty, tw, 26, 10, GLASS_ALL, 6, RGB(36, 36, 42), 150, 30);
        font_draw(tx + 11, ty + 4, nm, RGB(245, 245, 248), FONT_REGULAR);
    }
}

/* ---------- "джинн": сворачивание в Dock ---------- */

static int dock_icon_center(int app, i32 *cx, i32 *cy, i32 *iw);

/* Снимок окна ровно таким, каким его видно (или увидят) в кадре, и запуск анимации. */
static void genie_begin(int app, int dir)
{
    window_t *w = &wins[app];
    int top = top_app();
    fb_restore_background();
    int idx = nz;                               /* при сворачивании окна выше него в снимок не входят */
    for (int i = 0; i < nz; i++)
        if (zorder[i] == app) idx = i;
    for (int i = 0; i < nz; i++) {
        int a = zorder[i];
        if (a == app || wins[a].minimized || ((dir == 1 || dir == 3) && i > idx)) continue;
        draw_window(a, a == top);
    }
    int foc = dir < 0 || dir == 2 || app == top;
    draw_window(app, foc);
    genie_capture(w->x, w->y, w->w, w->h, WIN_R, foc ? 96 : 50);
    gn_app = app;
    gn_dir = dir;
    gn_t0 = now_ticks;
    scene_dirty = 1;
}

static void genie_finish(void)
{
    if (gn_app < 0) return;
    if (gn_dir < 0 || gn_dir == 2) {
        wins[gn_app].minimized = 0;
        raise_window(gn_app);
    }
    gn_app = -1;
    scene_dirty = 1;
}

static int genie_p(void)                        /* 0..1024 по ходу анимации */
{
    i32 p = (i32)(now_ticks - gn_t0) * 1024 / (i32)GN_DUR;
    if (p < 0) p = 0;
    return p > 1024 ? 1024 : p;
}

static u32 genie_dur(void) { return gn_dir == 2 || gn_dir == 3 ? GZ_DUR : GN_DUR; }

static void genie_render(void)
{
    if (gn_app < 0 || lp_open) return;
    if (gn_dir >= 2) {                              /* окно появляется или исчезает: масштаб и прозрачность */
        i32 e = anim_ease_out((i32)(now_ticks - gn_t0) * 1024 / (i32)GZ_DUR);
        if (gn_dir == 2) genie_zoom(880 + (1024 - 880) * e / 1024, e * 2 > 1024 ? 1024 : e * 2);
        else             genie_zoom(1024 - (1024 - 900) * e / 1024, 1024 - e);
        return;
    }
    i32 cx, cy, iw;
    if (!dock_icon_center(gn_app, &cx, &cy, &iw)) return;
    int p = genie_p();
    genie_draw(cx, cy, iw, gn_dir > 0 ? p : 1024 - p);
}

/* ---------- Mission Control ---------- */

static void mc_layout(void)
{
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    i32 order[APP_COUNT];                                   /* слева направо */
    for (int i = 0; i < mc_n; i++) order[i] = mc_ids[i];
    for (int i = 0; i < mc_n; i++)
        for (int j = i + 1; j < mc_n; j++)
            if (wins[order[j]].x + wins[order[j]].w / 2 < wins[order[i]].x + wins[order[i]].w / 2) {
                i32 t = order[i]; order[i] = order[j]; order[j] = t;
            }
    if (mc_n == 0) return;
    const i32 gap = 28, lab = 34;
    i32 ax = 48, aw = W - 96, ay = MENUBAR_H + 70, ah = H - ay - 120;
    int best_rows = 1;
    i32 best_s = 0;
    for (int rows = 1; rows <= 3 && rows <= mc_n; rows++) {
        int per = (mc_n + rows - 1) / rows;
        i32 s = 1024, sum_mh = 0;
        for (int r = 0; r < rows; r++) {
            i32 sw = 0, mh = 0, cnt = 0;
            for (int k = r * per; k < (r + 1) * per && k < mc_n; k++) {
                sw += wins[order[k]].w; cnt++;
                if (wins[order[k]].h > mh) mh = wins[order[k]].h;
            }
            if (!cnt) continue;
            i32 sr = (aw - (cnt - 1) * gap) * 1024 / (sw > 0 ? sw : 1);
            if (sr < s) s = sr;
            sum_mh += mh;
        }
        i32 sh = (ah - rows * lab - (rows - 1) * gap) * 1024 / (sum_mh > 0 ? sum_mh : 1);
        if (sh < s) s = sh;
        if (s > 740) s = 740;
        if (s > best_s) { best_s = s; best_rows = rows; }
    }
    i32 s = best_s < 120 ? 120 : best_s;
    int rows = best_rows, per = (mc_n + rows - 1) / rows;
    i32 tot = 0, mhs[3] = { 0, 0, 0 };
    for (int r = 0; r < rows; r++) {
        for (int k = r * per; k < (r + 1) * per && k < mc_n; k++)
            if (wins[order[k]].h * s / 1024 > mhs[r]) mhs[r] = wins[order[k]].h * s / 1024;
        tot += mhs[r] + lab;
    }
    tot += (rows - 1) * gap;
    i32 y = ay + (ah - tot) / 2;
    for (int r = 0; r < rows; r++) {
        i32 rw = 0, cnt = 0;
        for (int k = r * per; k < (r + 1) * per && k < mc_n; k++) { rw += wins[order[k]].w * s / 1024; cnt++; }
        rw += (cnt - 1) * gap;
        i32 x = ax + (aw - rw) / 2;
        for (int k = r * per; k < (r + 1) * per && k < mc_n; k++) {
            int a = order[k];
            mc_w[a] = wins[a].w * s / 1024;
            mc_h[a] = wins[a].h * s / 1024;
            mc_x[a] = x;
            mc_y[a] = y + (mhs[r] - mc_h[a]) / 2;
            x += mc_w[a] + gap;
        }
        y += mhs[r] + lab + gap;
    }
}

static void mc_open_now(void)
{
    cc_close();
    menu_close();
    if (lp_open) lp_close();
    genie_finish();
    max_finish();
    drag_app = -1;
    mc_reset();
    mc_n = 0;
    int top = top_app();
    for (int i = 0; i < nz; i++) {
        int a = zorder[i];
        if (!wins[a].open || wins[a].minimized) continue;
        window_t *w = &wins[a];
        i32 ox = w->x, oy = w->y;                           /* окно за краем экрана снимаем целиком */
        i32 W = (i32)fb_width(), H = (i32)fb_height();
        if (w->x + w->w > W) w->x = W - w->w;
        if (w->x < 0) w->x = 0;
        if (w->y + w->h > H) w->y = H - w->h;
        if (w->y < 0) w->y = 0;
        fb_restore_background();
        draw_window(a, a == top);                           /* окно одно, как оно выглядит само по себе */
        int ok = mc_capture(a, w->x, w->y, w->w, w->h, WIN_R);
        w->x = ox; w->y = oy;
        if (ok) mc_ids[mc_n++] = a;
    }
    mc_layout();
    mc_on = 1;
    mc_hover = -1;
    scene_dirty = 1;
}

static void mc_close_now(int select)
{
    if (select >= 0) raise_window(select);                  /* выбранное окно возвращается наверх */
    mc_on = 0;
    mc_hover = -1;
    scene_dirty = 1;
}

static void mc_toggle(void)
{
    if (mc_on) mc_close_now(-1);
    else mc_open_now();
}

static int mc_hit(i32 x, i32 y)
{
    for (int k = mc_n - 1; k >= 0; k--) {
        int a = mc_ids[k];
        if (x >= mc_x[a] && x < mc_x[a] + mc_w[a] && y >= mc_y[a] && y < mc_y[a] + mc_h[a])
            return a;
    }
    return -1;
}

static void mc_label(const char *txt, i32 cx, i32 y)
{
    i32 tw = font_width(txt, FONT_REGULAR) + 22;
    i32 x = cx - tw / 2;
    fb_glass(x, y, tw, 24, 12, GLASS_ALL, 6, RGB(30, 30, 36), 140, 20);
    font_draw(x + 11, y + 3, txt, RGB(245, 245, 248), FONT_REGULAR);
}

static void mc_render(void)
{
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    i32 e = mc_v;
    fb_blend_rect(0, 0, W, H, RGB(10, 10, 24), (u32)(110 * e / 1024));
    for (int i = 0; i < nz; i++) {
        int a = zorder[i];
        int in = 0;
        for (int k = 0; k < mc_n; k++) if (mc_ids[k] == a) in = 1;
        if (!in) continue;
        const window_t *w = &wins[a];
        i32 x = w->x + (mc_x[a] - w->x) * e / 1024, y = w->y + (mc_y[a] - w->y) * e / 1024;
        i32 ww = w->w + (mc_w[a] - w->w) * e / 1024, hh = w->h + (mc_h[a] - w->h) * e / 1024;
        if (a == mc_hover && e >= 1000) {                   /* рамка выбора */
            fb_fill_round_rect(x - 5, y - 5, ww + 10, hh + 10, 12, RGB(255, 255, 255), 190);
        }
        fb_shadow(x, y, ww, hh, 8, 22, 40 + 70 * (e >= 1024 ? 1024 : e) / 1024);
        mc_draw(a, x << 8, y << 8, ww << 8, hh << 8, 1024);
    }
    if (e > 640) {                                          /* подписи появляются, когда окна почти на местах */
        i32 al = (e - 640) * 1024 / 384;
        for (int k = 0; k < mc_n; k++) {
            int a = mc_ids[k];
            const char *t = app_name[a];
            i32 tw = font_width(t, FONT_REGULAR) + 22, cx = mc_x[a] + mc_w[a] / 2, ly = mc_y[a] + mc_h[a] + 8;
            anim_region_begin(cx - tw / 2 - 2, ly - 2, tw + 4, 28);
            mc_label(t, cx, ly);
            anim_region_end(0, al, 0);
        }
        i32 pw = 120;
        anim_region_begin(W / 2 - pw / 2 - 2, MENUBAR_H + 14, pw + 4, 32);
        mc_label("Desktop 1", W / 2, MENUBAR_H + 16);
        anim_region_end(0, al, 0);
    }
    if (mc_n == 0 && e > 640) {
        const char *m = "No Open Windows";
        font_draw(W / 2 - font_width(m, FONT_REGULAR) / 2, H / 2 - 8, m, RGB(225, 225, 235), FONT_REGULAR);
    }
}

/* ---------- menu bar ---------- */

static i32 cc_icon_x(void)
{
    char clock[48];
    format_clock(clock, 0);
    return (i32)fb_width() - 14 - font_width(clock, FONT_REGULAR) - 18 - 22;
}


/* ---------- выпадающие меню ---------- */

enum { MENU_LOGO, MENU_APP, MENU_FILE, MENU_EDIT, MENU_VIEW, MENU_WINDOW, MENU_HELP, MENU_COUNT };
enum { M_NONE, M_ABOUT, M_SETTINGS, M_RESTART, M_SHUTDOWN, M_QUIT, M_HIDE, M_CLOSEWIN,
       M_NEWFOLDER, M_FULL, M_DARK, M_MIN, M_ZOOM, M_HELP, M_MC, M_OPEN = 100 };

typedef struct { const char *label; int id; int disabled; } mitem_t;

static int    menu_open = -1, menu_hover = -1;
static i32    mt_x0[MENU_COUNT], mt_x1[MENU_COUNT];
static mitem_t mitems[16];
static int    mn;

static void mi(const char *label, int id, int disabled)
{
    mitems[mn].label = label;
    mitems[mn].id = id;
    mitems[mn].disabled = disabled;
    mn++;
}

static void build_menu(int m)
{
    int top = top_app();
    mn = 0;
    switch (m) {
    case MENU_LOGO:
        mi("About EndixOS PC", M_ABOUT, 0);
        mi(0, 0, 0);
        mi("System Settings...", M_SETTINGS, 0);
        mi(0, 0, 0);
        mi("Restart...", M_RESTART, 0);
        mi("Shut Down...", M_SHUTDOWN, 0);
        break;
    case MENU_APP:
        mi("About EndixOS PC", M_ABOUT, 0);
        mi(0, 0, 0);
        mi(theme_is_dark() ? "Switch to Light Mode" : "Switch to Dark Mode", M_DARK, 0);
        mi(0, 0, 0);
        mi("Hide", M_HIDE, top < 0);
        mi("Quit", M_QUIT, top < 0);
        break;
    case MENU_FILE:
        mi("Open Files", M_OPEN + APP_FILES, 0);
        mi("Open Terminal", M_OPEN + APP_TERMINAL, 0);
        mi("New Folder", M_NEWFOLDER, top != APP_FILES);
        mi(0, 0, 0);
        mi("Close Window", M_CLOSEWIN, top < 0);
        break;
    case MENU_EDIT:                               /* буфера обмена пока нет */
        mi("Undo", M_NONE, 1);
        mi("Redo", M_NONE, 1);
        mi(0, 0, 0);
        mi("Cut", M_NONE, 1);
        mi("Copy", M_NONE, 1);
        mi("Paste", M_NONE, 1);
        mi("Select All", M_NONE, 1);
        break;
    case MENU_VIEW:
        mi(top >= 0 && wins[top].maximized ? "Exit Full Screen" : "Enter Full Screen", M_FULL, top < 0);
        mi(0, 0, 0);
        mi(theme_is_dark() ? "Light Mode" : "Dark Mode", M_DARK, 0);
        break;
    case MENU_WINDOW:
        mi("Minimize", M_MIN, top < 0);
        mi("Zoom", M_ZOOM, top < 0);
        mi("Mission Control", M_MC, 0);
        mi(0, 0, 0);
        for (int i = 0; i < APP_COUNT; i++)
            if (wins[i].open)
                mi(app_name[i], M_OPEN + i, 0);
        break;
    case MENU_HELP:
        mi("EndixOS Help", M_HELP, 0);
        mi("About EndixOS PC", M_ABOUT, 0);
        break;
    }
}

#define MI_H   22
#define MI_SEP 9

static void menu_rect(int m, i32 *x, i32 *y, i32 *w, i32 *h)
{
    build_menu(m);
    i32 mw = 150, mh = 12;
    for (int i = 0; i < mn; i++) {
        if (mitems[i].label) {
            i32 lw = font_width(mitems[i].label, FONT_REGULAR) + 40;
            if (lw > mw) mw = lw;
            mh += MI_H;
        } else {
            mh += MI_SEP;
        }
    }
    *x = mt_x0[m] < 4 ? 4 : mt_x0[m];
    *y = MENUBAR_H + 1;
    *w = mw;
    *h = mh;
}

/* Какой пункт под курсором (-1 - никакой). Список пунктов должен быть построен. */
static int menu_item_at(int m, i32 px, i32 py)
{
    i32 x, y, w, h;
    menu_rect(m, &x, &y, &w, &h);
    if (px < x || px >= x + w || py < y || py >= y + h)
        return -1;
    i32 iy = y + 6;
    for (int i = 0; i < mn; i++) {
        i32 ih = mitems[i].label ? MI_H : MI_SEP;
        if (mitems[i].label && py >= iy && py < iy + ih)
            return i;
        iy += ih;
    }
    return -1;
}

static void draw_menu_dropdown(int m)
{
    const theme_t *t = theme();
    i32 x, y, w, h;
    menu_rect(m, &x, &y, &w, &h);
    for (int k = 0; k < 5; k++)
        fb_fill_round_rect(x - k, y - k + 4, w + 2 * k, h + 2 * k, 9 + k, RGB(0, 0, 0), 10);
    fb_glass(x, y, w, h, 12, GLASS_ALL, 10, theme_is_dark() ? RGB(44, 44, 50) : RGB(244, 244, 247),
             theme_is_dark() ? 170 : 172, 30);
    i32 iy = y + 6;
    for (int i = 0; i < mn; i++) {
        if (!mitems[i].label) {
            fb_blend_rect(x + 10, iy + MI_SEP / 2, w - 20, 1, RGB(128, 128, 128), 70);
            iy += MI_SEP;
            continue;
        }
        int hot = (i == menu_hover) && !mitems[i].disabled;
        if (hot)
            fb_fill_round_rect(x + 5, iy, w - 10, MI_H, 5, theme_accent(), 255);
        font_draw(x + 16, iy + 2, mitems[i].label,
                  hot ? RGB(255, 255, 255) : (mitems[i].disabled ? t->sub : t->text), FONT_REGULAR);
        iy += MI_H;
    }
}

static void menu_close(void)
{
    menu_open = -1;
    menu_hover = -1;
}

static void shutdown_now(void)
{
    fb_fill_rect(0, 0, (i32)fb_width(), (i32)fb_height(), RGB(0, 0, 0));
    const char *m = "It is now safe to turn off your computer.";
    font_draw((i32)fb_width() / 2 - font_width(m, FONT_REGULAR) / 2, (i32)fb_height() / 2, m,
              RGB(200, 200, 200), FONT_REGULAR);
    fb_flip();
    sys_shutdown();
}

static void menu_exec(int id)
{
    int top = top_app();
    switch (id) {
    case M_ABOUT:    open_window(APP_ABOUT); break;
    case M_SETTINGS: open_window(APP_SETTINGS); break;
    case M_RESTART:  sys_reboot(); break;
    case M_SHUTDOWN: shutdown_now(); break;
    case M_QUIT:     if (top >= 0) close_window(top); break;
    case M_HIDE:     if (top >= 0) minimize_window(top); break;
    case M_CLOSEWIN: if (top >= 0) close_window(top); break;
    case M_MIN:      if (top >= 0) minimize_window(top); break;
    case M_MC:       mc_toggle(); break;
    case M_ZOOM:
    case M_FULL:     if (top >= 0) toggle_maximize(top); break;
    case M_DARK:     theme_set_dark(!theme_is_dark()); break;
    case M_NEWFOLDER:
        if (top == APP_FILES) files_new_folder();
        break;
    case M_HELP:
        open_window(APP_TERMINAL);
        break;
    default:
        if (id >= M_OPEN && id < M_OPEN + APP_COUNT)
            open_window(id - M_OPEN);
    }
}

static int menu_title_at(i32 px, i32 py)
{
    if (py >= MENUBAR_H)
        return -1;
    for (int i = 0; i < MENU_COUNT; i++)
        if (px >= mt_x0[i] && px < mt_x1[i])
            return i;
    return -1;
}

static void draw_menubar(void)
{
    i32 W = (i32)fb_width();
    const theme_t *t = theme();
    const u32 ink = t->mbar_ink;

    fb_glass(0, 0, W, MENUBAR_H, 0, 16, 8, t->mbar_tint, theme_is_dark() ? 120 : 125, 0);
    fb_blend_rect(0, MENUBAR_H - 1, W, 1, RGB(0, 0, 0), theme_is_dark() ? 90 : 30);

    logo_draw(11, 4, 16, theme_accent());

    mt_x0[0] = 6; mt_x1[0] = 34;
    int top = top_app();
    static const char *const menus[] = { "File", "Edit", "View", "Window", "Help" };
    i32 x = 40;
    for (int i = 1; i < MENU_COUNT; i++) {
        const char *nm = i == 1 ? (top >= 0 ? app_name[top] : "EndixOS") : menus[i - 2];
        int st = i == 1 ? FONT_BOLD : FONT_REGULAR;
        i32 w = font_width(nm, st);
        mt_x0[i] = x - 8;
        mt_x1[i] = x + w + 8;
        if (menu_open == i)
            fb_fill_round_rect(mt_x0[i], 2, w + 16, MENUBAR_H - 4, 5, ink, 45);
        font_draw(x, 3, nm, ink, st);
        x += w + 18;
    }
    if (menu_open == 0)
        fb_fill_round_rect(mt_x0[0], 2, 28, MENUBAR_H - 4, 5, ink, 45);

    char clock[48];
    format_clock(clock, 0);
    font_draw(W - 14 - font_width(clock, FONT_REGULAR), 3, clock, ink, FONT_REGULAR);
    if (cc_is_open())
        fb_fill_round_rect(cc_icon_x() - 6, 2, 30, MENUBAR_H - 5, 5, ink, 50);
    cc_draw_icon(cc_icon_x(), ink);
    cc_draw_search_icon(cc_icon_x() - 30, 3, ink);
    if (sp_on) fb_fill_round_rect(cc_icon_x() - 36, 2, 26, MENUBAR_H - 5, 5, ink, 50);
}


/* ---------- Launchpad ---------- */

#define LP_ICON  96
#define LP_CELLW 160
#define LP_CELLH 156
#define LP_MAXC  7

static int  lp_open;
static char lp_q[24];
static int  lp_qn;
static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int lp_list(int *out)
{
    int n = 0;
    for (int a = 0; a < APP_COUNT; a++) {
        const char *nm = app_name[a];
        int ok = lp_qn == 0;
        for (int i = 0; !ok && nm[i]; i++) {
            int j = 0;
            while (j < lp_qn && nm[i + j] && lc(nm[i + j]) == lc(lp_q[j])) j++;
            ok = j == lp_qn;
        }
        if (ok) out[n++] = a;
    }
    return n;
}

static void lp_cell(int k, int n, i32 *x, i32 *y)
{
    int cols = n < LP_MAXC ? n : LP_MAXC;
    int rows = (n + cols - 1) / cols;
    *x = ((i32)fb_width() - cols * LP_CELLW) / 2 + (k % cols) * LP_CELLW;
    *y = ((i32)fb_height() - rows * LP_CELLH) / 2 + (k / cols) * LP_CELLH;
}

static int lp_hit(i32 px, i32 py)
{
    int list[APP_COUNT], n = lp_list(list);
    for (int k = 0; k < n; k++) {
        i32 x, y;
        lp_cell(k, n, &x, &y);
        if (px >= x + 8 && px < x + LP_CELLW - 8 && py >= y && py < y + LP_CELLH - 8)
            return list[k];
    }
    return -1;
}

static void lp_draw_icon(int app, i32 x, i32 y)
{
    icon_draw(app, x, y, LP_ICON);
}

static void lp_ring(i32 cx, i32 cy)
{
    for (i32 j = -8; j <= 8; j++)
        for (i32 i = -8; i <= 8; i++) {
            i32 d = i * i + j * j;
            if (d >= 25 && d <= 49)
                fb_blend_pixel(cx + i, cy + j, RGB(235, 235, 245), 220);
        }
    for (i32 k = 0; k < 5; k++)
        fb_fill_rect(cx + 6 + k, cy + 6 + k, 2, 2, RGB(235, 235, 245));
}

static void draw_launchpad(void)
{
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    fb_blend_rect(0, 0, W, H, RGB(18, 16, 40), 120);          /* затемнение вместо размытия */

    i32 sw = 260, sx = W / 2 - sw / 2, sy = 34;
    fb_glass(sx, sy, sw, 30, 15, GLASS_ALL, 6, RGB(255, 255, 255), 40, 34);
    lp_ring(sx + 24, sy + 13);
    if (lp_qn) {
        i32 tx = font_draw(sx + 46, sy + 6, lp_q, RGB(255, 255, 255), FONT_REGULAR);
        fb_fill_rect(tx + 2, sy + 7, 1, 16, RGB(255, 255, 255));
    } else {
        font_draw(sx + 46, sy + 6, "Search", RGB(200, 200, 215), FONT_REGULAR);
    }

    int list[APP_COUNT], n = lp_list(list);
    int hov = lp_hit(cur_x, cur_y);
    for (int k = 0; k < n; k++) {
        i32 x, y;
        lp_cell(k, n, &x, &y);
        i32 ix = x + (LP_CELLW - LP_ICON) / 2;
        if (list[k] == hov)
            fb_fill_round_rect(ix - 10, y - 8, LP_ICON + 20, LP_CELLH - 10, 22, RGB(255, 255, 255), 34);
        lp_draw_icon(list[k], ix, y);
        const char *nm = app_name[list[k]];
        font_draw(x + LP_CELLW / 2 - font_width(nm, FONT_REGULAR) / 2 + 1, y + LP_ICON + 13, nm, RGB(0, 0, 0), FONT_REGULAR);
        font_draw(x + LP_CELLW / 2 - font_width(nm, FONT_REGULAR) / 2, y + LP_ICON + 12, nm, RGB(255, 255, 255), FONT_REGULAR);
    }
    if (n == 0) {
        const char *m = "No results";
        font_draw(W / 2 - font_width(m, FONT_REGULAR) / 2, H / 2 - 8, m, RGB(210, 210, 225), FONT_REGULAR);
    }
    fb_fill_circle(W / 2, H - 112, 4, RGB(255, 255, 255));    /* точка страницы */
}

static void lp_show(void)
{
    if (mc_on) mc_close_now(-1);
    cc_close();
    menu_close();
    lp_open = 1;
    lp_qn = 0;
    lp_q[0] = 0;
}

static void lp_close(void)
{
    lp_open = 0;
}

static void lp_key(i32 c)
{
    int list[APP_COUNT], n;
    if (c == 27) { lp_close(); return; }
    if (c == '\n') {
        n = lp_list(list);
        if (n > 0) { lp_close(); open_window(list[0]); }
        return;
    }
    if (c == '\b') {
        if (lp_qn) lp_q[--lp_qn] = 0;
    } else if (c >= 32 && c < 127 && lp_qn < 22) {
        lp_q[lp_qn++] = (char)c;
        lp_q[lp_qn] = 0;
    }
}

#include "spotlight.inc"

/* ---------- содержимое окон ---------- */

static u32 term_color(u8 a)
{
    switch (a) {
    case TC_GREEN:  return RGB(95, 220, 125);
    case TC_BLUE:   return RGB(110, 165, 255);
    case TC_RED:    return RGB(255, 105, 97);
    case TC_YELLOW: return RGB(255, 208, 90);
    case TC_ACCENT: return theme_accent_color(theme_accent_index());
    case TC_CYAN:   return RGB(95, 215, 230);
    case TC_GRAY:   return RGB(135, 135, 148);
    case TC_WHITE:  return RGB(250, 250, 252);
    default:        return RGB(225, 225, 230);
    }
}

static void draw_terminal(i32 ox, i32 oy, int focused)
{
    if (cm_on) {
        cm_draw(ox, oy, content_w(APP_TERMINAL), content_h(APP_TERMINAL));
        return;
    }
    i32 x0 = ox + 10, y0 = oy + 8;
    char run[TCOLS + 1];
    for (int r = 0; r < trows; r++) {
        const char *row;
        const u8 *at;
        int v = (ring_n - tview) + r;                   /* строка прокрутки назад или живая */
        if (v < ring_n) {
            int idx = ((ring_head - ring_n + v) % RING_N + RING_N) % RING_N;
            row = ring_ch[idx];
            at = ring_at[idx];
        } else {
            row = tbuf[v - ring_n];
            at = tab_[v - ring_n];
        }
        int c = 0;
        while (c < tcols) {                             /* куски одного цвета рисуем одной строкой */
            if (row[c] == ' ') { c++; continue; }
            int s = c;
            u8 a = at[c];
            while (c < tcols && at[c] == a && !(row[c] == ' ' && (c + 1 >= tcols || row[c + 1] == ' ' || at[c + 1] != a)))
                c++;
            int n = c - s;
            memcpy(run, &row[s], (size_t)n);
            run[n] = 0;
            font_draw(x0 + s * FONT_MONO_ADV, y0 + r * 15, run, term_color(a), FONT_MONO);
        }
    }
    if (focused && tview == 0)                      /* блок-курсор */
        fb_blend_rect(x0 + tcol * FONT_MONO_ADV, y0 + trow * 15, FONT_MONO_ADV, 15,
                      RGB(220, 220, 230), 170);
    if (tview > 0) {                                /* подсказка при прокрутке назад */
        char b[40];
        char *q = u_put(b, (u32)tview);
        s_put(q, " lines up");
        i32 tw = font_width(b, FONT_REGULAR) + 20, cw = content_w(APP_TERMINAL);
        fb_glass(ox + cw - tw - 12, oy + 8, tw, 22, 11, GLASS_ALL, 4, RGB(30, 30, 36), 150, 20);
        font_draw(ox + cw - tw - 2, oy + 11, b, RGB(235, 235, 240), FONT_REGULAR);
    }
}

static int window_at(i32 x, i32 y);

/* Участок фона под стеклом: запоминаем до рисования тела окна и возвращаем перед стеклом. */
static u32 gl_save_t[FB_MAX_W * TITLE_H], gl_save_s[200 * 800];
static const i32 app_sidebar[APP_COUNT] = { 150, 0, 170, 0, 0 };

static void rect_save(u32 *buf, i32 x, i32 y, i32 w, i32 h)
{
    u32 *fbuf = fb_buffer();
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    for (i32 j = 0; j < h; j++) {
        if (y + j < 0 || y + j >= H) continue;
        for (i32 i = 0; i < w; i++)
            if (x + i >= 0 && x + i < W)
                buf[j * w + i] = fbuf[(y + j) * W + x + i];
    }
}

/* Возвращает фон только внутри скруглённого прямоугольника: углы вне окна остаются с тенью и обводкой. */
static void rect_restore(const u32 *buf, i32 x, i32 y, i32 w, i32 h, i32 r, u32 corners)
{
    u32 *fbuf = fb_buffer();
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    i32 inset[32];
    if (r > 32) r = 32;
    for (i32 k = 0; k < r; k++) {
        i32 dy = r - 1 - k, i = 0;
        while (i < r && (r - 1 - i) * (r - 1 - i) + dy * dy >= r * r)
            i++;
        inset[k] = i;
    }
    for (i32 j = 0; j < h; j++) {
        if (y + j < 0 || y + j >= H) continue;
        i32 li = 0, ri = 0;
        if (j < r)           { li = (corners & 1) ? inset[j] : 0; ri = (corners & 2) ? inset[j] : 0; }
        else if (j >= h - r) { li = (corners & 4) ? inset[h - 1 - j] : 0; ri = (corners & 8) ? inset[h - 1 - j] : 0; }
        for (i32 i = li; i < w - ri; i++)
            if (x + i >= 0 && x + i < W)
                fbuf[(y + j) * W + x + i] = buf[j * w + i];
    }
}

/* ---------- изменение размера окон ---------- */

enum { E_L = 1, E_R = 2, E_T = 4, E_B = 8 };

static int rs_app = -1, rs_edges;
static i32 rs_mx, rs_my, rs_x, rs_y, rs_w, rs_h;

/* Какие края окна под точкой (0 - никакие). */
static int edges_of(int app, i32 x, i32 y)
{
    const window_t *w = &wins[app];
    if (!app_resizable[app] || w->maximized || w->minimized || !w->open)
        return 0;
    if (x < w->x - 4 || x >= w->x + w->w + 4 || y < w->y - 4 || y >= w->y + w->h + 4)
        return 0;
    int e = 0;
    if (x < w->x + 6) e |= E_L;
    if (x >= w->x + w->w - 6) e |= E_R;
    if (y < w->y + 4) e |= E_T;
    if (y >= w->y + w->h - 6) e |= E_B;
    if (e && !(e & (E_L | E_R)) && (e & E_B)) {           /* нижние углы делаем пошире */
        if (x < w->x + 14) e |= E_L;
        if (x >= w->x + w->w - 14) e |= E_R;
    }
    if (e && !(e & (E_T | E_B)) && (e & (E_L | E_R)) && y >= w->y + w->h - 14)
        e |= E_B;
    return e;
}

/* Окно с краем под курсором: самое верхнее из подходящих. */
static int edge_window(i32 x, i32 y, int *edges)
{
    for (int k = nz - 1; k >= 0; k--) {
        int a = zorder[k];
        const window_t *w = &wins[a];
        if (w->minimized)
            continue;
        int inside = x >= w->x - 4 && x < w->x + w->w + 4 && y >= w->y - 4 && y < w->y + w->h + 4;
        if (!inside)
            continue;
        int e = edges_of(a, x, y);
        if (e) {
            *edges = e;
            return a;
        }
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h)
            return -1;                                    /* верхнее окно закрывает нижние */
    }
    return -1;
}

static void resize_start(int app, int edges)
{
    const window_t *w = &wins[app];
    rs_app = app;
    rs_edges = edges;
    rs_mx = cur_x;
    rs_my = cur_y;
    rs_x = w->x;
    rs_y = w->y;
    rs_w = w->w;
    rs_h = w->h;
}

static void resize_move(void)
{
    window_t *w = &wins[rs_app];
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    i32 dx = cur_x - rs_mx, dy = cur_y - rs_my;
    i32 nx = rs_x, ny = rs_y, nw = rs_w, nh = rs_h;
    if (rs_edges & E_R) nw = rs_w + dx;
    if (rs_edges & E_L) nw = rs_w - dx;
    if (rs_edges & E_B) nh = rs_h + dy;
    if (rs_edges & E_T) nh = rs_h - dy;
    if (nw < app_minw[rs_app]) nw = app_minw[rs_app];
    if (nh < app_minh[rs_app]) nh = app_minh[rs_app];
    if (nw > W) nw = W;
    if (nh > H - MENUBAR_H) nh = H - MENUBAR_H;
    if (!(rs_edges & E_L) && nx + nw > W) nw = W - nx;        /* справа и снизу не уходим за экран */
    if (!(rs_edges & E_T) && ny + nh > H) nh = H - ny;
    if (nw < app_minw[rs_app]) nw = app_minw[rs_app];      /* минимум важнее границ экрана */
    if (nh < app_minh[rs_app]) nh = app_minh[rs_app];
    if (rs_edges & E_L) nx = rs_x + rs_w - nw;
    if (rs_edges & E_T) ny = rs_y + rs_h - nh;
    if (ny < MENUBAR_H) {
        ny = MENUBAR_H;
        nh = rs_y + rs_h - ny;
        if (!(rs_edges & E_T)) nh = rs_h;
    }
    w->x = nx;
    w->y = ny;
    w->w = nw;
    w->h = nh;
}

static void sync_sizes(void)
{
    if (wins[APP_FILES].open)
        files_set_size(content_w(APP_FILES), content_h(APP_FILES));
    if (wins[APP_TERMINAL].open)
        term_resize(content_w(APP_TERMINAL), content_h(APP_TERMINAL));
}

/* Окно, у которого курсор над группой "светофора" (-1 - нет). */
static int tl_hot(void)
{
    int app = window_at(cur_x, cur_y);
    if (app < 0 || wins[app].minimized)
        return -1;
    i32 rx = cur_x - wins[app].x, ry = cur_y - wins[app].y;
    return rx >= 8 && rx < 72 && ry >= 4 && ry < TITLE_H - 4 ? app : -1;
}

static void draw_window(int app, int focused)
{
    const window_t *w = &wins[app];
    int dark = (app == APP_TERMINAL);

    /* Заголовок и боковая панель - стекло: размывают то, что лежит ПОД окном. Фон запоминаем до тени и тела. */
    i32 sbw = app_sidebar[app], sbh = w->h - TITLE_H;
    if (sbh > 800) sbh = 800;
    rect_save(gl_save_t, w->x, w->y, w->w, TITLE_H);
    if (sbw)
        rect_save(gl_save_s, w->x, w->y + TITLE_H, sbw, sbh);

    /* Тень как в macOS: широкая и мягкая, у неактивного окна слабее. Рамки с k <= 0 лежат под окном. */
    fb_shadow(w->x, w->y, w->w, w->h, 10, 28, focused ? 96 : 50);
    fb_fill_round_rect(w->x - 1, w->y - 1, w->w + 2, w->h + 2, WIN_R + 1, RGB(0, 0, 0), focused ? 70 : 45);   /* тонкая обводка */

    /* терминал всегда тёмный, остальные окна следуют теме */
    const theme_t *th = dark ? theme_variant(1) : theme();
    int chrome_dark = dark || theme_is_dark();
    u32 body  = dark ? RGB(28, 28, 33) : th->body;
    u32 title = focused ? th->title_f : th->title_u;
    u32 ttext = focused ? th->ttext_f : th->ttext_u;

    fb_fill_round_rect(w->x, w->y, w->w, w->h, WIN_R, body, 255);

    rect_restore(gl_save_t, w->x, w->y, w->w, TITLE_H, WIN_R, 3);
    fb_glass(w->x, w->y, w->w, TITLE_H, WIN_R, 3, 8, dark ? RGB(44, 44, 50) : title, focused ? (dark ? 170 : 150) : 215, 34);
    if (sbw) {
        rect_restore(gl_save_s, w->x, w->y + TITLE_H, sbw, sbh, WIN_R, 4);
        fb_glass(w->x, w->y + TITLE_H, sbw, sbh, WIN_R, 4 | 16, 8, th->side, 178, 0);
    }
    fb_blend_rect(w->x, w->y + TITLE_H - 1, w->w, 1, RGB(0, 0, 0), th->line_alpha);

    /* "светофор": 12 px, шаг 20 px; при наведении на группу показываются знаки */
    static const u32 tl_fill[3] = { RGB(255, 95, 86), RGB(255, 189, 46), RGB(39, 201, 63) };
    static const u32 tl_ring[3] = { RGB(224, 68, 62), RGB(222, 161, 35), RGB(30, 168, 50) };
    static const u32 tl_ink[3]  = { RGB(100, 10, 6),  RGB(110, 70, 0),   RGB(8, 80, 14) };
    u32 grey = chrome_dark ? RGB(88, 88, 94) : RGB(206, 206, 211);
    int hot = tl_hot() == app;
    for (int b = 0; b < 3; b++) {
        i32 cx = w->x + 20 + b * 20, cy = w->y + TITLE_H / 2;
        int on = focused || hot;
        fb_fill_circle(cx, cy, 6, on ? tl_ring[b] : (chrome_dark ? RGB(70, 70, 76) : RGB(190, 190, 196)));
        fb_fill_circle(cx, cy, 5, on ? tl_fill[b] : grey);
        if (hot) {
            for (i32 k = -2; k <= 2; k++) {
                if (b == 0) {
                    fb_fill_rect(cx + k, cy + k, 1, 1, tl_ink[b]);
                    fb_fill_rect(cx + k, cy - k, 1, 1, tl_ink[b]);
                } else if (b == 1) {
                    fb_fill_rect(cx + k, cy, 1, 1, tl_ink[b]);
                } else {
                    fb_fill_rect(cx + k, cy, 1, 1, tl_ink[b]);
                    fb_fill_rect(cx, cy + k, 1, 1, tl_ink[b]);
                }
            }
        }
    }

    i32 tw = font_width(app_title[app], FONT_BOLD);
    font_draw(w->x + (w->w - tw) / 2, w->y + (TITLE_H - font_height[FONT_BOLD]) / 2,
              app_title[app], ttext, FONT_BOLD);

    i32 ox, oy, cw = content_w(app), ch = content_h(app);
    content_origin(app, &ox, &oy);
    switch (app) {
    case APP_ABOUT:    about_window_draw(ox, oy, cw, ch); break;
    case APP_TERMINAL: draw_terminal(ox, oy, focused); break;
    case APP_FILES:    files_draw(ox, oy); break;
    case APP_SETTINGS: settings_draw(ox, oy, cw, ch); break;
    case APP_SNAKE:    snake_draw(ox, oy, cw, ch); break;
    }
}

/* ---------- курсор ---------- */

static const char *const cursor_img[17] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.....XXXXX ",
    "X..X..X     ",
    "X.X X..X    ",
    "XX  X..X    ",
    "X    X..X   ",
    "     X..X   ",
    "      XX    ",
};

#define CURSOR_W 12
#define CURSOR_H 17

/* Форма стрелки изменения размера: 1 - влево-вправо, 2 - вверх-вниз, 3 - NWSE, 4 - NESW. */
static int resize_cursor_kind(void)
{
    int e = 0;
    if (rs_app >= 0)
        e = rs_edges;
    else if (!lp_open && menu_open < 0 && !cc_hit((i32)fb_width(), cur_x, cur_y))
        edge_window(cur_x, cur_y, &e);
    if (!e) return 0;
    int h = e & (E_L | E_R), v = e & (E_T | E_B);
    if (h && !v) return 1;
    if (v && !h) return 2;
    return ((e & E_L) != 0) == ((e & E_T) != 0) ? 3 : 4;
}

static int rc_in(int kind, i32 dx, i32 dy)          /* точка (dx,dy) принадлежит стрелке? */
{
    i32 ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (kind == 1) return (ax <= 4 && ay <= 1) || (ax >= 3 && ax <= 7 && ay <= 7 - ax);
    if (kind == 2) return (ay <= 4 && ax <= 1) || (ay >= 3 && ay <= 7 && ax <= 7 - ay);
    i32 u = kind == 3 ? dx + dy : dx - dy, v = kind == 3 ? dx - dy : dx + dy;
    i32 au = u < 0 ? -u : u, av = v < 0 ? -v : v;
    return (au <= 8 && av <= 2) || (au >= 7 && au <= 13 && av <= 13 - au);
}

/* Курсор рисуется прямо в кадр, поэтому его движение не требует перерисовки сцены. */
static void draw_cursor(void)
{
    int kind = resize_cursor_kind();
    if (kind) {
        for (i32 j = -9; j <= 9; j++)
            for (i32 i = -9; i <= 9; i++) {
                if (rc_in(kind, i, j)) {
                    fb_frame_put_pixel(cur_x + i, cur_y + j, RGB(255, 255, 255));
                } else {
                    for (i32 k = -1; k <= 1; k++)
                        for (i32 m = -1; m <= 1; m++)
                            if (rc_in(kind, i + m, j + k)) {
                                fb_frame_put_pixel(cur_x + i, cur_y + j, RGB(0, 0, 0));
                                k = 2;
                                break;
                            }
                }
            }
        drawn_x = cur_x - 10;
        drawn_y = cur_y - 10;
        drawn_w = drawn_h = 21;
        return;
    }
    for (int j = 0; j < CURSOR_H; j++)
        for (int i = 0; cursor_img[j][i]; i++) {
            char c = cursor_img[j][i];
            if (c == 'X')
                fb_frame_put_pixel(cur_x + i, cur_y + j, RGB(0, 0, 0));
            else if (c == '.')
                fb_frame_put_pixel(cur_x + i, cur_y + j, RGB(255, 255, 255));
        }
    drawn_x = cur_x;
    drawn_y = cur_y;
    drawn_w = CURSOR_W;
    drawn_h = CURSOR_H;
}

/* ---------- публичный интерфейс ---------- */

void wm_init(const sysinfo_t *info)
{
    sys = *info;
    settings_init(&sys);            /* превью обоев и первые обои на экране */
    widgets_init(&sys);
    fs_init();
    cwd = fs_home();
    files_init();
    snake_reset();

    cur_x = (i32)sys.width / 2;
    cur_y = (i32)sys.height / 2;

    term_clear();
    term_cputs("EndixOS " ENDIX_VERSION, TC_ACCENT);
    term_puts(" - type 'help' for commands\n");
    term_prompt();
    tend_r = trow;
    tend_c = tcol;

    open_window(APP_ABOUT);
    open_window(APP_TERMINAL);
    bn_app = -1;                            /* при загрузке значки не прыгают */
    booting = 0;
}

int wm_set_time(u32 hour, u32 min, u32 day, u32 month, u32 year)
{
    int changed = !have_time || hour != t_hour || min != t_min || day != t_day;
    t_hour = hour; t_min = min; t_day = day; t_mon = month; t_year = year;
    widgets_set_time(hour, min, day, month, year);
    have_time = 1;
    return changed;
}

void wm_set_uptime(u32 seconds)
{
    uptime_s = seconds;
    widgets_set_uptime(seconds);
    if (widgets_visible()) scene_dirty = 1;
}

/* Плавные значения: меню, Control Center, Launchpad, увеличение Dock. 1 - что-то ещё движется. */
static int anim_update(u32 ticks)
{
    u32 dt = (i32)(ticks - an_last) > 0 ? ticks - an_last : 0;
    an_last = ticks;
    int moving = 0;
    if (menu_open >= 0) mn_last = menu_open;
    i32 n;
    n = anim_approach(mn_v, menu_open >= 0 ? 1024 : 0, dt);  moving |= n != mn_v || (n != 0 && n != 1024); mn_v = n;
    n = anim_approach(cc_v, cc_is_open() ? 1024 : 0, dt);    moving |= n != cc_v || (n != 0 && n != 1024); cc_v = n;
    n = anim_approach(mc_v, mc_on ? 1024 : 0, dt);           moving |= n != mc_v || (n != 0 && n != 1024); mc_v = n;
    n = anim_approach(lp_v, lp_open ? 1024 : 0, dt);         moving |= n != lp_v || (n != 0 && n != 1024); lp_v = n;
    moving |= desktop_tick(dt);
    moving |= widgets_tick(dt);
    moving |= nt_tick(dt);
    n = anim_approach(sp_v, sp_on ? 1024 : 0, dt);          moving |= n != sp_v || (n != 0 && n != 1024); sp_v = n;
    i32 H = (i32)fb_height();
    {   /* автоскрытие и умное скрытие Dock */
        i32 rx, ry, rw, rh;
        dock_rest_rect(&rx, &ry, &rw, &rh);
        int want_hidden = 0;
        if (dock_hide) {
            int near = cur_y >= H - 3 || (dock_hide_v < 700 && cur_y >= ry - 10);   /* у нижнего края или над самим Dock */
            if (near) dock_hold = ticks + 25;
            int busy = lp_open || mc_on || (gn_app >= 0 && gn_dir >= -1 && gn_dir <= 1 && gn_dir != 0) || (i32)(ticks - dock_hold) < 0;
            want_hidden = !busy && (dock_hide == 1 || dock_covers_window());
        }
        n = anim_approach(dock_hide_v, want_hidden ? 1024 : 0, dt);
        if (n != dock_hide_v || (n != 0 && n != 1024)) moving = 1;
        dock_hide_v = n;
        dock_off = (rh + 16) * dock_hide_v / 1024;
    }
    int inband = dock_magnify && cur_y >= H - DOCK_BAND && !lp_open && dock_hide_v < 300;
    if (inband && dk_mag_v == 0) dk_cx16 = cur_x << 4;                     /* вход в зону: без "разгона" с места */
    n = anim_approach(dk_mag_v, inband ? 1024 : 0, dt);
    if (n != dk_mag_v || (n != 0 && n != 1024)) moving = 1;
    dk_mag_v = n;
    if (inband) {
        i32 want = cur_x << 4, d = want - dk_cx16;
        for (u32 k = 0; k < (dt > 12 ? 12 : dt) && d; k++) {
            i32 st = d / 2;
            if (st == 0) st = d > 0 ? 1 : -1;
            dk_cx16 += st;
            d = want - dk_cx16;
        }
        if (d) moving = 1;
    }
    return moving;
}

int wm_tick(u32 ticks)
{
    now_ticks = ticks;
    int gn = anim_update(ticks);
    max_step();
    if (ma_app >= 0) gn = 1;
    if (bn_app >= 0) {
        gn = 1;
        if ((i32)(ticks - bn_t0) >= BN_DUR) bn_app = -1;
    }
    if (gn_app >= 0) {
        gn = 1;
        if ((i32)(ticks - gn_t0) >= (i32)genie_dur()) genie_finish();
    }
    int active = wins[APP_SNAKE].open && top_app() == APP_SNAKE;
    int cm = 0;
    if (cm_on && wins[APP_TERMINAL].open && !wins[APP_TERMINAL].minimized && (i32)(ticks - cm_last) >= 6) {
        cm_last = ticks;
        cm_step();
        cm = 1;
    }
    if (nj_wait && !net_job_active()) {            /* фоновая сетевая команда закончилась */
        nj_wait = 0;
        term_prompt();
        tend_r = trow; tend_c = tcol;
        cm = 1;
    }
    if (nj_dirty) { nj_dirty = 0; cm = 1; }
    return snake_tick(ticks, active) | cm | gn;
}

static int window_at(i32 x, i32 y)
{
    for (int i = nz - 1; i >= 0; i--) {
        const window_t *w = &wins[zorder[i]];
        if (w->minimized)
            continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h)
            return zorder[i];
    }
    return -1;
}

static void desktop_action(int act)
{
    switch (act) {
    case DK_ACT_NEWFOLDER: {
        int d = fs_resolve(fs_home(), "Desktop");
        if (d >= 0) {
            char nm[40];
            fs_unique_name(d, "untitled folder", nm);
            fs_mkdir(d, nm);
        }
        break;
    }
    case DK_ACT_WALLPAPER: settings_show_page(0); open_window(APP_SETTINGS); break;
    case DK_ACT_APPEAR:    settings_show_page(1); open_window(APP_SETTINGS); break;
    case DK_ACT_GLASS:     settings_show_page(2); open_window(APP_SETTINGS); break;
    case DK_ACT_EDITWIDGETS: widgets_edit_start(); break;
    case DK_ACT_REMOVEWIDGET: widgets_remove(desktop_ctx_widget()); break;
    }
}

static void on_press(void)
{
    i32 W = (i32)fb_width();
    if (sp_on) {                                         /* Spotlight: строка результата или клик мимо */
        int r = sp_row_at(cur_x, cur_y);
        if (r >= 0) sp_run(r);
        else if (!sp_inside(cur_x, cur_y)) sp_close();
        return;
    }
    if (widgets_edit_active()) {                         /* редактор виджетов */
        if (cur_y >= MENUBAR_H) widgets_edit_press(cur_x, cur_y);
        return;
    }
    if (nt_hit(cur_x, cur_y)) { nt_on = 0; return; }
    if (cur_y < MENUBAR_H && cur_x >= cc_icon_x() - 38 && cur_x < cc_icon_x() - 8) {
        sp_toggle();
        return;
    }
    if (desktop_ctx_is_open()) {                         /* контекстное меню: пункт или клик мимо */
        desktop_action(desktop_ctx_click(cur_x, cur_y));
        return;
    }
    if (lp_open) {
        int a = lp_hit(cur_x, cur_y);
        lp_close();
        if (a >= 0) open_window(a);
        return;
    }
    int mt = menu_title_at(cur_x, cur_y);
    if (mt >= 0) {
        cc_close();
        if (menu_open == mt) menu_close();
        else { menu_open = mt; menu_hover = -1; }
        return;
    }
    if (menu_open >= 0) {
        int it = menu_item_at(menu_open, cur_x, cur_y);
        int id = it >= 0 && !mitems[it].disabled ? mitems[it].id : 0;
        if (it >= 0 && mitems[it].disabled) return;       /* серый пункт меню не закрывает */
        menu_close();
        if (id) menu_exec(id);
        return;
    }
    if (cur_y < MENUBAR_H && cur_x >= cc_icon_x() - 8 && cur_x < cc_icon_x() + 26) {
        cc_toggle();
        return;
    }
    if (cc_is_open()) {
        if (cc_hit(W, cur_x, cur_y))
            cc_press(W, cur_x, cur_y);
        else
            cc_close();
        return;
    }
    if (dock_hit(cur_x, cur_y) >= 0) {
        int d = dock_hit(cur_x, cur_y);
        if (d == LP_ID) lp_show();
        else open_window(d);
        return;
    }
    int re = 0, ra = edge_window(cur_x, cur_y, &re);
    if (ra >= 0 && re) {                                /* край окна: тянем размер */
        raise_window(ra);
        resize_start(ra, re);
        return;
    }
    int app = window_at(cur_x, cur_y);
    if (app < 0) {                                      /* рабочий стол: выбор значка, двойной клик открывает в Files */
        if (cur_y >= MENUBAR_H) {
            int n = desktop_press(cur_x, cur_y, now_ticks);
            if (n >= 0) {
                open_window(APP_FILES);
                files_goto(n);
            }
        }
        return;
    }
    desktop_deselect();

    raise_window(app);
    window_t *w = &wins[app];
    i32 rx = cur_x - w->x, ry = cur_y - w->y;

    /* "светофор": закрыть, свернуть в Dock, развернуть / вернуть размер */
    if (ry < TITLE_H) {
        for (int b = 0; b < 3; b++) {
            i32 dx = rx - (20 + b * 20), dy = ry - TITLE_H / 2;
            if (dx * dx + dy * dy > 9 * 9)
                continue;
            if (b == 0)      close_window(app);
            else if (b == 1) minimize_window(app);
            else             toggle_maximize(app);
            return;
        }
        if (app == last_title_app && (i32)(now_ticks - last_title_tick) <= 40) {
            last_title_app = -1;                       /* двойной клик по заголовку */
            toggle_maximize(app);
            return;
        }
        last_title_app = app;
        last_title_tick = now_ticks;
        if (!w->maximized) {
            drag_app = app;
            drag_dx = rx;
            drag_dy = ry;
        }
        return;
    }

    i32 ox, oy;
    content_origin(app, &ox, &oy);
    i32 cx = cur_x - ox, cy = cur_y - oy;
    if (cx < 0 || cy < 0 || cx >= content_w(app) || cy >= content_h(app))
        return;                                         /* поле вокруг содержимого развёрнутого окна */
    if (app == APP_ABOUT) {
        if (about_window_click(cx, cy, app_w[APP_ABOUT]))
            open_window(APP_SETTINGS);
    } else if (app == APP_SETTINGS)
        settings_click(cx, cy);
    else if (app == APP_FILES)
        files_click(cx, cy, now_ticks);
    else if (app == APP_SNAKE)
        snake_click();
}

void wm_handle_event(const event_t *e)
{
    sync_sizes();
    if (e->type == EV_MOUSE) {
        int hover_before = dock_hit(cur_x, cur_y), tl_before = tl_hot();
        if (cur_x != e->a || cur_y != e->b)
            cursor_moved = 1;
        cur_x = e->a;
        cur_y = e->b;
        u8 btn = (u8)e->c;
        if (btn || btn_prev || dock_hit(cur_x, cur_y) != hover_before || tl_hot() != tl_before ||
            (cursor_moved && cur_y >= (i32)fb_height() - DOCK_BAND))
            scene_dirty = 1;                /* ещё: волна увеличения Dock */                /* перетаскивание, клик или подсветка в Dock */

        if (menu_open >= 0) {                       /* подсветка пункта и переход между меню */
            int mt = menu_title_at(cur_x, cur_y);
            if (mt >= 0 && mt != menu_open) { menu_open = mt; menu_hover = -1; }
            int hv = menu_item_at(menu_open, cur_x, cur_y);
            if (hv != menu_hover) menu_hover = hv;
            scene_dirty = 1;
        }
        if (lp_open)
            scene_dirty = 1;                        /* подсветка значка под курсором */
        {
            static u8 rprev;
            int rp = (btn & 2) && !(rprev & 2);
            rprev = btn;
            if (desktop_ctx_is_open()) { desktop_ctx_hover(cur_x, cur_y); scene_dirty = 1; }
            if (sp_on) { int hr = sp_row_at(cur_x, cur_y); if (hr >= 0 && hr != sp_sel && cursor_moved) sp_sel = hr; scene_dirty = 1; }
            if (rp && !mc_on && !lp_open && menu_open < 0 && !cc_is_open()) {
                if (widgets_edit_active()) {
                    /* в режиме редактирования правая кнопка ничего не открывает */
                } else if (window_at(cur_x, cur_y) < 0 && cur_y >= MENUBAR_H && dock_hit(cur_x, cur_y) < 0) {
                    int wi = widgets_hit(cur_x, cur_y);
                    if (wi >= 0) desktop_ctx_open_widget(cur_x, cur_y, wi);
                    else desktop_ctx_open(cur_x, cur_y);
                    desktop_deselect();
                } else desktop_ctx_close();
                scene_dirty = 1;
            }
        }
        int pressed  = (btn & 1) && !(btn_prev & 1);
        int released = !(btn & 1) && (btn_prev & 1);
        btn_prev = btn;

        if (mc_on || mc_v > 0) {                    /* Mission Control: наведение, выбор окна, клик мимо закрывает */
            int h = mc_on && mc_v >= 900 ? mc_hit(cur_x, cur_y) : -1;
            if (h != mc_hover) mc_hover = h;
            scene_dirty = 1;
            if (pressed && mc_on) {
                int a = mc_hit(cur_x, cur_y);
                mc_close_now(a);
            }
            return;
        }
        if (pressed) {
            on_press();
        } else if ((btn & 1) && drag_app >= 0) {
            window_t *w = &wins[drag_app];
            i32 W = (i32)fb_width(), H = (i32)fb_height();
            i32 nx = cur_x - drag_dx, ny = cur_y - drag_dy;
            if (ny < MENUBAR_H) ny = MENUBAR_H;
            if (ny > H - 40) ny = H - 40;
            if (nx < 60 - w->w) nx = 60 - w->w;
            if (nx > W - 60) nx = W - 60;
            w->x = nx;
            w->y = ny;
        } else if ((btn & 1) && rs_app >= 0) {
            resize_move();
        } else if ((btn & 1) && cc_dragging()) {
            cc_drag((i32)fb_width(), cur_x);
        } else if ((btn & 1) && settings_dragging()) {
            i32 ox, oy;
            content_origin(APP_SETTINGS, &ox, &oy);
            settings_drag(cur_x - ox);
        }
        if (released) {
            drag_app = -1;
            rs_app = -1;
            cc_release();
            settings_release();
        }
    } else if (e->type == EV_KEY) {
        scene_dirty = 1;
        if (e->a == KEY_F3 || (e->a == KEY_UP && e->b)) {      /* F3 или Ctrl+Вверх */
            mc_toggle();
            return;
        }
        if (e->a == ' ' && e->b) {                         /* Ctrl+Пробел */
            sp_toggle();
            return;
        }
        if (sp_on) {
            sp_key(e->a);
            return;
        }
        if (widgets_edit_active()) {
            if (e->a == 27) widgets_edit_stop();
            return;
        }
        if (mc_on) {
            if (e->a == 27) mc_close_now(-1);
            return;
        }
        if (desktop_ctx_is_open()) {
            if (e->a == 27) desktop_ctx_close();
            return;
        }
        if (lp_open) {
            lp_key(e->a);
            return;
        }
        if (menu_open >= 0) {
            if (e->a == 27) menu_close();
            return;
        }
        if (cc_is_open()) {                         /* Esc закрывает панель, остальное ей не нужно */
            if (e->a == 27) cc_close();
            return;
        }
        switch (top_app()) {
        case APP_TERMINAL: term_key(e->a, e->b); break;
        case APP_SNAKE:    snake_key(e->a);      break;
        case APP_FILES:    files_key(e->a);      break;
        default: break;
        }
    }
}

static void render_scene(int mode)                  /* 0 окна, 1 Launchpad вместо окон, 2 Mission Control */
{
    int lp = mode == 1;
    fb_restore_background();
    desktop_draw((i32)fb_width(), (i32)fb_height());
    widgets_draw(0);
    int top = top_app();
    for (int i = 0; i < nz; i++)
        if (mode == 0 && !wins[zorder[i]].minimized)
            draw_window(zorder[i], zorder[i] == top);
    if (mode == 2) mc_render();
    else genie_render();
    draw_dock();
    desktop_ctx_draw();
    if (cc_brightness() < 100)                      /* яркость: затемнение всего экрана */
        fb_blend_rect(0, 0, (i32)fb_width(), (i32)fb_height(), 0, (u32)(100 - cc_brightness()) * 2);
    draw_menubar();
    if (lp) {
        draw_launchpad();
        return;
    }
    widgets_draw(1);
    nt_draw();
    sp_draw();
    if (cc_v > 0) {                                 /* Control Center выезжает из-под меню-бара */
        i32 rx, ry, rw, rh;
        cc_bounds((i32)fb_width(), &rx, &ry, &rw, &rh);
        if (cc_v >= 1024) cc_draw_any((i32)fb_width());
        else {
            anim_region_begin(rx, ry, rw, rh);
            cc_draw_any((i32)fb_width());
            anim_region_end(-(1024 - cc_v) * 18 / 1024, cc_v, MENUBAR_H);
        }
    }
    int m = menu_open >= 0 ? menu_open : mn_last;
    if (mn_v > 0 && m >= 0) {
        i32 x, y, w, h;
        menu_rect(m, &x, &y, &w, &h);
        if (mn_v >= 1024) draw_menu_dropdown(m);
        else {
            anim_region_begin(x - 8, y - 4, w + 16, h + 18);
            draw_menu_dropdown(m);
            anim_region_end(-(1024 - mn_v) * 8 / 1024, mn_v, MENUBAR_H);
        }
    }
}

void wm_render(void)
{
    sync_sizes();
    if (mc_v > 0) {
        render_scene(2);
    } else if (lp_v > 0 && lp_v < 1024) {                  /* Launchpad плавно проявляется поверх рабочего стола */
        render_scene(0);
        anim_screen_save();
        render_scene(1);
        anim_screen_blend(lp_v);
    } else {
        render_scene(lp_open ? 1 : 0);
    }
    scene_dirty = 0;
    cursor_moved = 0;
}

int wm_get_option(int id)
{
    return id == WM_OPT_DOCK_MAGNIFY ? dock_magnify : id == WM_OPT_CLOCK24 ? clock24 :
           id == WM_OPT_DOCK_ZOOM ? dock_zoom : id == WM_OPT_DOCK_HIDE ? dock_hide : reduce_transp;
}

void wm_set_option(int id, int v)
{
    if (id == WM_OPT_DOCK_MAGNIFY) dock_magnify = v ? 1 : 0;
    else if (id == WM_OPT_CLOCK24) clock24 = v ? 1 : 0;
    else if (id == WM_OPT_DOCK_HIDE) dock_hide = v < 0 ? 0 : (v > 2 ? 2 : v);
    else if (id == WM_OPT_DOCK_ZOOM) dock_zoom = v < 0 ? 0 : (v > 100 ? 100 : v);
    else { reduce_transp = v ? 1 : 0; fb_glass_set_reduced(reduce_transp); }
    scene_dirty = 1;
}

void wm_invalidate(void)
{
    scene_dirty = 1;
}

/* Один кадр, если есть что показать. Сцена не чаще раза в 20 мс, курсор - сразу. */
int wm_frame(u32 ticks)
{
    if (scene_dirty && (i32)(ticks - last_scene_tick) >= 2) {
        last_scene_tick = ticks;
        wm_render();
        fb_flip();
        draw_cursor();
        cursor_moved = 0;
        return 1;
    }
    if (cursor_moved) {                     /* курсор не ждёт сцену */
        cursor_moved = 0;
        fb_frame_restore(drawn_x, drawn_y, drawn_w, drawn_h);
        draw_cursor();
        return 1;
    }
    return 0;
}
