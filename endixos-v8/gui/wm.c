/* Оконный менеджер EndixOS: menu bar, окна, Dock, курсор, встроенные приложения. */
#include "wm.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "settings.h"
#include "wallpaper.h"
#include "snake.h"
#include "files.h"
#include "../kernel/fs.h"
#include "../kernel/string.h"

#define MENUBAR_H 24
#define TITLE_H   34
#define WIN_R     10

enum { APP_FILES, APP_TERMINAL, APP_SETTINGS, APP_SNAKE, APP_ABOUT, APP_COUNT };

typedef struct {
    int open;
    int minimized;               /* свёрнуто в Dock: не рисуется и не получает ввод */
    int maximized;               /* развёрнуто на весь экран */
    i32 x, y, w, h;
    i32 sx, sy;                  /* позиция до разворачивания */
} window_t;

static const char *const app_name[APP_COUNT]  = { "Files", "Terminal", "Settings", "Snake", "About" };
static const char *const app_title[APP_COUNT] = { "Files", "Terminal", "Settings", "Snake", "About EndixOS" };
static const i32 app_w[APP_COUNT] = { FILES_W, 580, 640, 380, 440 };
static const i32 app_h[APP_COUNT] = { FILES_H + TITLE_H, 360, 440, 400, 280 };

static void open_window(int app);

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
static int       last_title_app = -1;       /* для двойного клика по заголовку */
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
    p = u2_put(p, t_hour);
    *p++ = ':';
    p = u2_put(p, t_min);
    *p = 0;
}

/* ---------- терминал ---------- */

#define TCOLS 80
#define TROWS 20
#define TLINE 70

static char tbuf[TROWS][TCOLS];
static int  trow, tcol;
static char tline[TLINE + 1];
static int  tlen;

static void term_scroll(void)
{
    memmove(&tbuf[0][0], &tbuf[1][0], (TROWS - 1) * TCOLS);
    memset(&tbuf[TROWS - 1][0], ' ', TCOLS);
}

static void term_newline(void)
{
    tcol = 0;
    if (++trow >= TROWS) {
        term_scroll();
        trow = TROWS - 1;
    }
}

static void term_putc(char c)
{
    if (c == '\n') {
        term_newline();
        return;
    }
    if (tcol >= TCOLS)
        term_newline();
    tbuf[trow][tcol++] = c;
}

static void term_puts(const char *s)
{
    while (*s) term_putc(*s++);
}

static void term_prompt(void)
{
    char path[96], home[48], out[128];
    char *p = s_put(out, "endix@v8 ");

    if (!fs_is_dir(cwd))                    /* папку удалили из Files */
        cwd = fs_home();
    fs_path(cwd, path, sizeof(path));
    fs_path(fs_home(), home, sizeof(home));
    int hl = s_len(home);
    int under_home = 1;
    for (int i = 0; i < hl; i++)
        if (path[i] != home[i])
            under_home = 0;
    if (under_home && (path[hl] == 0 || path[hl] == '/')) {
        *p++ = '~';
        p = s_put(p, path + hl);
    } else {
        p = s_put(p, path);
    }
    s_put(p, " % ");
    term_puts(out);
}

static void term_clear(void)
{
    memset(tbuf, ' ', sizeof(tbuf));
    trow = tcol = 0;
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
            if (tcol + s_len(name) + 3 > TCOLS)      /* не рвём имя на границе строки */
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

static void term_run(char *line)
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

    char buf[96];
    if (s_eq(line, "help")) {
        term_puts("Files:    ls [-l] cd pwd cat mkdir touch rm [-r] mv cp\n");
        term_puts("          echo text > file     echo text >> file\n");
        term_puts("Programs: snake  files\n");
        term_puts("Look:     wallpaper 1-6   theme light|dark\n");
        term_puts("System:   uname uptime date cpu mem clear reboot\n");
    } else if (s_eq(line, "snake")) {
        open_window(APP_SNAKE);
    } else if (s_eq(line, "files")) {
        open_window(APP_FILES);
    } else if (s_eq(line, "wallpaper")) {
        int id = (arg[0] >= '1' && arg[0] <= '0' + WALL_COUNT) ? arg[0] - '1' : -1;
        if (id < 0) {
            term_puts("usage: wallpaper 1-6 (");
            for (int i = 0; i < WALL_COUNT; i++) {
                term_puts(wallpaper_name(i));
                term_puts(i < WALL_COUNT - 1 ? ", " : ")\n");
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
    } else if (s_eq(line, "clear")) {
        term_clear();
    } else if (s_eq(line, "uname")) {
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
        term_puts("endix: command not found: ");
        term_puts(line);
        term_putc('\n');
    }
}

static void term_key(i32 c, i32 ctrl)
{
    if (ctrl && (c == 'l' || c == 'L')) {
        term_clear();
        term_prompt();
        tlen = 0;
        return;
    }
    if (c == '\n') {
        term_putc('\n');
        tline[tlen] = 0;
        term_run(tline);
        tlen = 0;
        term_prompt();
    } else if (c == '\b') {
        if (tlen > 0) {
            tlen--;
            if (tcol > 0) {
                tcol--;
                tbuf[trow][tcol] = ' ';
            }
        }
    } else if (c >= 32 && c < 127 && tlen < TLINE) {
        tline[tlen++] = (char)c;
        term_putc((char)c);
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
    if (w->open) {
        w->minimized = 0;                   /* клик по иконке в Dock возвращает свёрнутое окно */
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
}

static void close_window(int app)
{
    int i;
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
    wins[app].minimized = 1;
    drag_app = -1;
}

static void toggle_maximize(int app)
{
    window_t *w = &wins[app];
    if (w->maximized) {
        w->x = w->sx;
        w->y = w->sy;
        w->w = app_w[app];
        w->h = app_h[app];
        w->maximized = 0;
    } else {
        w->sx = w->x;
        w->sy = w->y;
        w->x = 0;
        w->y = MENUBAR_H;
        w->w = (i32)fb_width();
        w->h = (i32)fb_height() - MENUBAR_H - 84;     /* внизу остаётся место для Dock */
        w->maximized = 1;
    }
    drag_app = -1;
}

/* Левый верхний угол области содержимого. В развёрнутом окне содержимое стоит по центру. */
static void content_origin(int app, i32 *ox, i32 *oy)
{
    const window_t *w = &wins[app];
    *ox = w->x + (w->w - app_w[app]) / 2;
    *oy = w->y + TITLE_H + (w->h - app_h[app]) / 2;
}

/* ---------- Dock ---------- */

#define DOCK_ICON 52
#define DOCK_GAP  14
#define DOCK_PAD  14

static void dock_geom(i32 *dx, i32 *dy, i32 *dw, i32 *dh)
{
    *dw = APP_COUNT * DOCK_ICON + (APP_COUNT - 1) * DOCK_GAP + DOCK_PAD * 2;
    *dh = DOCK_ICON + DOCK_PAD;
    *dx = ((i32)fb_width() - *dw) / 2;
    *dy = (i32)fb_height() - *dh - 10;
}

static void dock_icon_pos(int i, i32 *ix, i32 *iy)
{
    i32 dx, dy, dw, dh;
    dock_geom(&dx, &dy, &dw, &dh);
    *ix = dx + DOCK_PAD + i * (DOCK_ICON + DOCK_GAP);
    *iy = dy + DOCK_PAD / 2;
}

static int dock_hit(i32 x, i32 y)
{
    for (int i = 0; i < APP_COUNT; i++) {
        i32 ix, iy;
        dock_icon_pos(i, &ix, &iy);
        if (x >= ix && x < ix + DOCK_ICON && y >= iy && y < iy + DOCK_ICON)
            return i;
    }
    return -1;
}

static void draw_icon_art(int app, i32 ix, i32 iy)
{
    const u32 white = RGB(255, 255, 255);
    i32 cx = ix + DOCK_ICON / 2, cy = iy + DOCK_ICON / 2;

    switch (app) {
    case APP_FILES:
        fb_fill_round_rect(ix, iy, DOCK_ICON, DOCK_ICON, 13, RGB(52, 120, 246), 255);
        fb_fill_round_rect(ix + 10, iy + 12, 15, 9, 3, white, 255);
        fb_fill_round_rect(ix + 10, iy + 17, 32, 23, 4, white, 255);
        break;
    case APP_TERMINAL:
        fb_fill_round_rect(ix, iy, DOCK_ICON, DOCK_ICON, 13, RGB(36, 36, 42), 255);
        for (i32 t = 0; t < 11; t++) {
            fb_fill_rect(ix + 13 + t, iy + 15 + t, 3, 3, white);
            fb_fill_rect(ix + 23 - t, iy + 26 + t, 3, 3, white);
        }
        fb_fill_rect(ix + 29, iy + 36, 11, 3, white);
        break;
    case APP_SETTINGS:
        fb_fill_round_rect(ix, iy, DOCK_ICON, DOCK_ICON, 13, RGB(128, 132, 144), 255);
        fb_fill_rect(cx - 3, cy - 20, 6, 8, white);
        fb_fill_rect(cx - 3, cy + 12, 6, 8, white);
        fb_fill_rect(cx - 20, cy - 3, 8, 6, white);
        fb_fill_rect(cx + 12, cy - 3, 8, 6, white);
        fb_fill_circle(cx, cy, 14, white);
        fb_fill_circle(cx, cy, 6, RGB(128, 132, 144));
        break;
    case APP_SNAKE:
        fb_fill_round_rect(ix, iy, DOCK_ICON, DOCK_ICON, 13, RGB(44, 190, 90), 255);
        for (i32 k = 0; k < 4; k++)
            fb_fill_round_rect(ix + 9 + k * 8, iy + 31, 7, 7, 2, white, 255);
        fb_fill_round_rect(ix + 41, iy + 23, 7, 15, 2, white, 255);
        fb_fill_round_rect(ix + 41, iy + 13, 7, 9, 2, RGB(255, 236, 120), 255);
        fb_fill_circle(ix + 15, iy + 17, 4, RGB(240, 70, 70));
        break;
    case APP_ABOUT:
        fb_fill_round_rect(ix, iy, DOCK_ICON, DOCK_ICON, 13, theme_accent(), 255);
        fb_fill_circle(cx, iy + 15, 4, white);
        fb_fill_round_rect(cx - 4, iy + 23, 8, 19, 3, white, 255);
        break;
    }
}

static void draw_dock(void)
{
    i32 dx, dy, dw, dh;
    dock_geom(&dx, &dy, &dw, &dh);
    const theme_t *t = theme();
    fb_fill_round_rect(dx - 1, dy - 1, dw + 2, dh + 2, 19, RGB(255, 255, 255), theme_is_dark() ? 30 : 50);
    fb_fill_round_rect(dx, dy, dw, dh, 18, t->dock_tint, t->dock_alpha);

    int hover = dock_hit(cur_x, cur_y);
    for (int i = 0; i < APP_COUNT; i++) {
        i32 ix, iy;
        dock_icon_pos(i, &ix, &iy);
        draw_icon_art(i, ix, iy);
        if (i == hover)
            fb_fill_round_rect(ix, iy, DOCK_ICON, DOCK_ICON, 13, RGB(255, 255, 255), 55);
        if (wins[i].open)
            fb_fill_circle(ix + DOCK_ICON / 2, dy + dh - 5, 2, theme_is_dark() ? RGB(235, 235, 240) : RGB(40, 40, 46));
    }
}

/* ---------- menu bar ---------- */

static void draw_menubar(void)
{
    i32 W = (i32)fb_width();
    const theme_t *t = theme();
    const u32 ink = t->mbar_ink;

    fb_blend_rect(0, 0, W, MENUBAR_H, t->mbar_tint, t->mbar_alpha);
    fb_blend_rect(0, MENUBAR_H - 1, W, 1, RGB(0, 0, 0), theme_is_dark() ? 90 : 35);

    fb_fill_round_rect(13, 5, 14, 14, 4, theme_accent(), 255);
    fb_fill_circle(20, 12, 3, RGB(255, 255, 255));

    int top = top_app();
    i32 x = 38;
    x = font_draw(x, 3, top >= 0 ? app_name[top] : "EndixOS", ink, FONT_BOLD) + 18;
    static const char *const menus[] = { "File", "Edit", "View", "Window", "Help" };
    for (int i = 0; i < 5; i++)
        x = font_draw(x, 3, menus[i], ink, FONT_REGULAR) + 16;

    char clock[48];
    format_clock(clock, 0);
    font_draw(W - 14 - font_width(clock, FONT_REGULAR), 3, clock, ink, FONT_REGULAR);
}

/* ---------- содержимое окон ---------- */

static void draw_terminal(i32 ox, i32 oy, int focused)
{
    i32 x0 = ox + 10, y0 = oy + 8;
    char ch[2] = { 0, 0 };
    for (int r = 0; r < TROWS; r++) {
        for (int c = 0; c < TCOLS; c++) {
            if (tbuf[r][c] == ' ')
                continue;
            ch[0] = tbuf[r][c];
            font_draw(x0 + c * FONT_MONO_ADV, y0 + r * 15, ch, RGB(225, 225, 230), FONT_MONO);
        }
    }
    if (focused)                                   /* блок-курсор */
        fb_blend_rect(x0 + tcol * FONT_MONO_ADV, y0 + trow * 15, FONT_MONO_ADV, 15,
                      RGB(220, 220, 230), 170);
}

static void draw_window(int app, int focused)
{
    const window_t *w = &wins[app];
    int dark = (app == APP_TERMINAL);

    /* Мягкая тень: концентрические рамки со смещением вниз. Рамки с k <= 0 лежат под
     * самим окном и нужны, чтобы у нижней кромки не было ступеньки; их закроет тело окна. */
    for (i32 k = -7; k <= 12; k++) {
        i32 a = 46 * (13 - k) / 20;
        if (a > 46) a = 46;
        if (!focused) a = a * 6 / 10;
        if (a <= 0) continue;
        i32 sy = w->y + 6;
        fb_blend_rect(w->x - k, sy - k, w->w + 2 * k, 1, RGB(0, 0, 0), (u32)a);
        fb_blend_rect(w->x - k, sy + w->h + k - 1, w->w + 2 * k, 1, RGB(0, 0, 0), (u32)a);
        fb_blend_rect(w->x - k, sy - k + 1, 1, w->h + 2 * k - 2, RGB(0, 0, 0), (u32)a);
        fb_blend_rect(w->x + w->w + k - 1, sy - k + 1, 1, w->h + 2 * k - 2, RGB(0, 0, 0), (u32)a);
    }

    /* терминал всегда тёмный, остальные окна следуют теме */
    const theme_t *th = dark ? theme_variant(1) : theme();
    int chrome_dark = dark || theme_is_dark();
    u32 body  = dark ? RGB(28, 28, 33) : th->body;
    u32 title = focused ? th->title_f : th->title_u;
    u32 ttext = focused ? th->ttext_f : th->ttext_u;

    fb_fill_round_rect(w->x, w->y, w->w, w->h, WIN_R, body, 255);
    fb_fill_round_rect(w->x, w->y, w->w, TITLE_H, WIN_R, title, 255);
    fb_fill_rect(w->x, w->y + WIN_R, w->w, TITLE_H - WIN_R, title);
    fb_blend_rect(w->x, w->y + TITLE_H - 1, w->w, 1, RGB(0, 0, 0), th->line_alpha);

    /* "светофор" */
    u32 grey = chrome_dark ? RGB(80, 80, 88) : RGB(205, 205, 210);
    fb_fill_circle(w->x + 20, w->y + 17, 6, focused ? RGB(255, 95, 87) : grey);
    fb_fill_circle(w->x + 40, w->y + 17, 6, focused ? RGB(254, 188, 46) : grey);
    fb_fill_circle(w->x + 60, w->y + 17, 6, focused ? RGB(40, 200, 64) : grey);

    i32 tw = font_width(app_title[app], FONT_BOLD);
    font_draw(w->x + (w->w - tw) / 2, w->y + (TITLE_H - font_height[FONT_BOLD]) / 2,
              app_title[app], ttext, FONT_BOLD);

    i32 ox, oy, cw = app_w[app], ch = app_h[app] - TITLE_H;
    content_origin(app, &ox, &oy);
    switch (app) {
    case APP_ABOUT:    about_draw(ox, oy); break;
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

static void draw_cursor(void)
{
    for (int j = 0; j < 17; j++)
        for (int i = 0; cursor_img[j][i]; i++) {
            char c = cursor_img[j][i];
            if (c == 'X')
                fb_put_pixel(cur_x + i, cur_y + j, RGB(0, 0, 0));
            else if (c == '.')
                fb_put_pixel(cur_x + i, cur_y + j, RGB(255, 255, 255));
        }
}

/* ---------- публичный интерфейс ---------- */

void wm_init(const sysinfo_t *info)
{
    sys = *info;
    settings_init(&sys);            /* превью обоев и первые обои на экране */
    fs_init();
    cwd = fs_home();
    files_init();
    snake_reset();

    cur_x = (i32)sys.width / 2;
    cur_y = (i32)sys.height / 2;

    term_clear();
    term_puts("EndixOS 8.0 - type 'help' for commands\n");
    term_prompt();

    open_window(APP_ABOUT);
    open_window(APP_TERMINAL);
}

int wm_set_time(u32 hour, u32 min, u32 day, u32 month, u32 year)
{
    int changed = !have_time || hour != t_hour || min != t_min || day != t_day;
    t_hour = hour; t_min = min; t_day = day; t_mon = month; t_year = year;
    have_time = 1;
    return changed;
}

void wm_set_uptime(u32 seconds)
{
    uptime_s = seconds;
}

int wm_tick(u32 ticks)
{
    now_ticks = ticks;
    int active = wins[APP_SNAKE].open && top_app() == APP_SNAKE;
    return snake_tick(ticks, active);
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

static void on_press(void)
{
    if (dock_hit(cur_x, cur_y) >= 0) {
        open_window(dock_hit(cur_x, cur_y));
        return;
    }
    int app = window_at(cur_x, cur_y);
    if (app < 0)
        return;

    raise_window(app);
    window_t *w = &wins[app];
    i32 rx = cur_x - w->x, ry = cur_y - w->y;

    /* "светофор": закрыть, свернуть в Dock, развернуть / вернуть размер */
    if (ry < TITLE_H) {
        for (int b = 0; b < 3; b++) {
            i32 dx = rx - (20 + b * 20), dy = ry - 17;
            if (dx * dx + dy * dy > 8 * 8)
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
    if (cx < 0 || cy < 0 || cx >= app_w[app] || cy >= app_h[app] - TITLE_H)
        return;                                         /* поле вокруг содержимого развёрнутого окна */
    if (app == APP_SETTINGS)
        settings_click(cx, cy);
    else if (app == APP_FILES)
        files_click(cx, cy, now_ticks);
    else if (app == APP_SNAKE)
        snake_click();
}

void wm_handle_event(const event_t *e)
{
    if (e->type == EV_MOUSE) {
        cur_x = e->a;
        cur_y = e->b;
        u8 btn = (u8)e->c;

        int pressed  = (btn & 1) && !(btn_prev & 1);
        int released = !(btn & 1) && (btn_prev & 1);
        btn_prev = btn;

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
        }
        if (released)
            drag_app = -1;
    } else if (e->type == EV_KEY) {
        switch (top_app()) {
        case APP_TERMINAL: term_key(e->a, e->b); break;
        case APP_SNAKE:    snake_key(e->a);      break;
        case APP_FILES:    files_key(e->a);      break;
        default: break;
        }
    }
}

void wm_render(void)
{
    fb_restore_background();
    int top = top_app();
    for (int i = 0; i < nz; i++)
        if (!wins[zorder[i]].minimized)
            draw_window(zorder[i], zorder[i] == top);
    draw_dock();
    draw_menubar();
    draw_cursor();
}
