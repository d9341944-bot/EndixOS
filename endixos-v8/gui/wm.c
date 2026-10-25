/* Оконный менеджер EndixOS: menu bar, окна, Dock, курсор, встроенные приложения. */
#include "wm.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "control.h"
#include "logo.h"
#include "settings.h"
#include "wallpaper.h"
#include "snake.h"
#include "files.h"
#include "../kernel/fs.h"
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
    i32 sx, sy;                  /* позиция до разворачивания */
} window_t;

static const char *const app_name[APP_COUNT]  = { "Files", "Terminal", "Settings", "Snake", "About" };
static const char *const app_title[APP_COUNT] = { "Files", "Terminal", "Settings", "Snake", "" };
static const i32 app_w[APP_COUNT] = { FILES_W, 580, 640, 380, 280 };
static const i32 app_h[APP_COUNT] = { FILES_H + TITLE_H, 360, 440, 400, 520 };

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
static int       dock_magnify = 1;          /* опции из Settings */
static int       clock24 = 1;
static int       scene_dirty = 1;           /* сцену нужно перерисовать */
static int       cursor_moved;              /* изменилась только позиция курсора */
static i32       drawn_x, drawn_y;          /* где курсор нарисован в кадре */
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
    cm_head[c] = (i16)(fresh ? -(i32)(cm_rand() % TROWS) : -(i32)(cm_rand() % 8));
    cm_len[c] = (u8)(5 + cm_rand() % 12);
    cm_spd[c] = (u8)(1 + cm_rand() % 3);
    cm_cnt[c] = 0;
}

static void cm_start(void)
{
    cm_rng ^= now_ticks * 2654435761u;
    if (!cm_rng) cm_rng = 1;
    for (int c = 0; c < TCOLS; c++)
        cm_column(c, 1);
    for (int r = 0; r < TROWS; r++)
        for (int c = 0; c < TCOLS; c++)
            cm_ch[r][c] = cm_glyph();
    cm_on = 1;
}

static void cm_step(void)
{
    for (int c = 0; c < TCOLS; c++) {
        if (++cm_cnt[c] < cm_spd[c])
            continue;
        cm_cnt[c] = 0;
        cm_head[c]++;
        if (cm_head[c] >= 0 && cm_head[c] < TROWS)
            cm_ch[cm_head[c]][c] = cm_glyph();
        if (cm_head[c] - cm_len[c] > TROWS)
            cm_column(c, 0);
    }
    for (int k = 0; k < 12; k++)                 /* мерцание: часть символов меняется на месте */
        cm_ch[cm_rand() % TROWS][cm_rand() % TCOLS] = cm_glyph();
}

static void cm_draw(i32 ox, i32 oy, i32 w, i32 h)
{
    fb_fill_rect(ox, oy, w, h, RGB(0, 0, 0));
    i32 x0 = ox + 10, y0 = oy + 8;
    char ch[2] = { 0, 0 };
    for (int c = 0; c < TCOLS; c++)
        for (int r = 0; r < TROWS; r++) {
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
        term_puts("System:   uname uptime date cpu mem clear reboot cmatrix\n");
    } else if (s_eq(line, "snake")) {
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
    if (cm_on) {                                   /* в cmatrix клавиши только для выхода */
        if (c == 'q' || c == 'Q' || c == 27 || (ctrl && (c == 'c' || c == 'C')))
            cm_on = 0;
        return;
    }
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

#define DOCK_MAX  80                  /* размер значка под курсором при увеличении */
#define DOCK_BAND 110                 /* высота зоны у нижнего края, где работает увеличение */
#define DOCK_WAVE 130                 /* радиус "волны" увеличения */

static i32 dk_x[DOCK_SLOTS], dk_s[DOCK_SLOTS];       /* левый край и размер значка в слоте */
static i32 dk_bx, dk_bw;                              /* плашка Dock */

/* Раскладка Dock для текущего положения курсора: значки у курсора растут, соседи раздвигаются. */
static void dock_layout(void)
{
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    i32 gw0 = DOCK_SLOTS * DOCK_ICON + (DOCK_SLOTS - 1) * DOCK_GAP;
    i32 bx0 = (W - gw0) / 2;
    int mag = dock_magnify && cur_y >= H - DOCK_BAND;
    i32 total = 0;
    for (int k = 0; k < DOCK_SLOTS; k++) {
        i32 sz = DOCK_ICON;
        if (mag) {
            i32 c = bx0 + k * (DOCK_ICON + DOCK_GAP) + DOCK_ICON / 2;
            i32 d = cur_x - c;
            if (d < 0) d = -d;
            if (d < DOCK_WAVE) {
                i32 f = (isin_w(d * 128 / DOCK_WAVE + 64) + 256) / 2;      /* 256 в центре, 0 на краю */
                sz = DOCK_ICON + (DOCK_MAX - DOCK_ICON) * f / 256;
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
    *dy = (i32)fb_height() - *dh - 8;
}

static void dock_icon_pos(int i, i32 *ix, i32 *iy)
{
    i32 dx, dy, dw, dh;
    dock_geom(&dx, &dy, &dw, &dh);
    int slot = i == LP_ID ? 0 : i + 1;
    *ix = dk_x[slot];
    *iy = dy + dh - DOCK_PAD * 3 / 4 - dk_s[slot];     /* значки стоят на одной линии, растут вверх */
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

static void draw_icon_art(int app, i32 ix, i32 iy)
{
    const u32 white = RGB(255, 255, 255);
    i32 cx = ix + DOCK_ICON / 2, cy = iy + DOCK_ICON / 2;

    switch (app) {
    case LP_ID: {                                       /* Launchpad: сетка цветных плиток */
        fb_fill_round_rect(ix, iy, DOCK_ICON, DOCK_ICON, 13, RGB(214, 218, 228), 255);
        static const u32 col[9] = { RGB(52,120,246), RGB(255,149,0), RGB(44,190,90), RGB(255,59,48), RGB(94,92,230),
                                    RGB(255,204,0), RGB(90,200,250), RGB(175,82,222), RGB(120,124,136) };
        for (int k = 0; k < 9; k++)
            fb_fill_round_rect(ix + 7 + (k % 3) * 14, iy + 7 + (k / 3) * 14, 11, 11, 3, col[k], 255);
        break;
    }
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
        fb_fill_round_rect(ix, iy, DOCK_ICON, DOCK_ICON, 13, theme_is_dark() ? RGB(52, 52, 60) : RGB(246, 246, 250), 255);
        logo_draw(ix + 6, iy + 6, DOCK_ICON - 12, theme_accent());
        break;
    }
}

static void icon_draw_scaled(int id, i32 x, i32 y, i32 size);
static void icons_ensure(void);

static void draw_dock(void)
{
    i32 dx, dy, dw, dh;
    dock_geom(&dx, &dy, &dw, &dh);
    const theme_t *t = theme();
    fb_fill_round_rect(dx - 1, dy - 1, dw + 2, dh + 2, 21, RGB(255, 255, 255), theme_is_dark() ? 38 : 70);
    fb_fill_round_rect(dx, dy, dw, dh, 20, t->dock_tint, t->dock_alpha);

    icons_ensure();
    int hover = dock_hit(cur_x, cur_y);
    for (int i = 0; i <= LP_ID; i++) {
        i32 ix, iy;
        dock_icon_pos(i, &ix, &iy);
        int slot = i == LP_ID ? 0 : i + 1;
        i32 sz = dk_s[slot];
        fb_blend_rect(ix + 4, iy + sz - 2, sz - 8, 3, RGB(0, 0, 0), 28);          /* лёгкая тень под значком */
        icon_draw_scaled(i, ix, iy, sz);
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
        fb_fill_round_rect(tx, ty, tw, 26, 8, RGB(40, 40, 46), 215);
        font_draw(tx + 11, ty + 4, nm, RGB(245, 245, 248), FONT_REGULAR);
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
       M_NEWFOLDER, M_FULL, M_DARK, M_MIN, M_ZOOM, M_HELP, M_OPEN = 100 };

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

static void draw_menu_dropdown(void)
{
    if (menu_open < 0)
        return;
    const theme_t *t = theme();
    i32 x, y, w, h;
    menu_rect(menu_open, &x, &y, &w, &h);
    for (int k = 0; k < 5; k++)
        fb_fill_round_rect(x - k, y - k + 4, w + 2 * k, h + 2 * k, 9 + k, RGB(0, 0, 0), 10);
    fb_fill_round_rect(x, y, w, h, 9, theme_is_dark() ? RGB(44, 44, 50) : RGB(240, 240, 243),
                       theme_is_dark() ? 235 : 238);
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

    fb_blend_rect(0, 0, W, MENUBAR_H, t->mbar_tint, t->mbar_alpha);
    fb_blend_rect(0, MENUBAR_H - 1, W, 1, RGB(0, 0, 0), theme_is_dark() ? 90 : 35);

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
}


/* ---------- Launchpad ---------- */

#define LP_ICON  96
#define LP_CELLW 160
#define LP_CELLH 156
#define LP_MAXC  7
#define SRC_S    DOCK_ICON

static int  lp_open;
static char lp_q[24];
static int  lp_qn;
static u32  lp_pm[APP_COUNT + 1][SRC_S * SRC_S];     /* значки на чёрном (premultiplied) */
static u8   lp_al[APP_COUNT + 1][SRC_S * SRC_S];
static int  icon_key = -1;     /* их прозрачность */

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

/* Значки рисуем штатным кодом на чёрном и на белом, отсюда берём цвет и прозрачность. */
static void lp_build_icons(void)
{
    u32 *fbuf = fb_buffer();
    u32 stride = fb_width();
    static u32 save[SRC_S * SRC_S], blk[SRC_S * SRC_S];
    for (int j = 0; j < SRC_S; j++)
        for (int i = 0; i < SRC_S; i++)
            save[j * SRC_S + i] = fbuf[j * stride + i];
    for (int a = 0; a <= APP_COUNT; a++) {
        fb_fill_rect(0, 0, SRC_S, SRC_S, 0);
        draw_icon_art(a, 0, 0);
        for (int j = 0; j < SRC_S; j++)
            for (int i = 0; i < SRC_S; i++)
                blk[j * SRC_S + i] = fbuf[j * stride + i];
        fb_fill_rect(0, 0, SRC_S, SRC_S, RGB(255, 255, 255));
        draw_icon_art(a, 0, 0);
        for (int j = 0; j < SRC_S; j++)
            for (int i = 0; i < SRC_S; i++) {
                u32 b = blk[j * SRC_S + i], w = fbuf[j * stride + i];
                u32 al = 255 - ((w >> 8 & 0xFF) - (b >> 8 & 0xFF));     /* по зелёному каналу */
                lp_pm[a][j * SRC_S + i] = b;
                lp_al[a][j * SRC_S + i] = (u8)al;
            }
    }
    for (int j = 0; j < SRC_S; j++)
        for (int i = 0; i < SRC_S; i++)
            fbuf[j * stride + i] = save[j * SRC_S + i];
}

static u32 bil(const u32 *pm, int a, int ch, i32 fx, i32 fy, int alpha_plane, const u8 *al)
{
    (void)a; (void)ch;
    i32 x0 = fx >> 8, y0 = fy >> 8, tx = fx & 255, ty = fy & 255;
    i32 x1 = x0 + 1 < SRC_S ? x0 + 1 : x0, y1 = y0 + 1 < SRC_S ? y0 + 1 : y0;
    u32 p[4];
    i32 xs[4] = { x0, x1, x0, x1 }, ys[4] = { y0, y0, y1, y1 };
    for (int k = 0; k < 4; k++)
        p[k] = alpha_plane ? al[ys[k] * SRC_S + xs[k]] : pm[ys[k] * SRC_S + xs[k]];
    u32 wgt[4] = { (u32)((256 - tx) * (256 - ty)), (u32)(tx * (256 - ty)), (u32)((256 - tx) * ty), (u32)(tx * ty) };
    if (alpha_plane) {
        u32 v = 0;
        for (int k = 0; k < 4; k++) v += p[k] * wgt[k];
        return v >> 16;
    }
    u32 r = 0, g = 0, b = 0;
    for (int k = 0; k < 4; k++) {
        r += (p[k] >> 16 & 0xFF) * wgt[k];
        g += (p[k] >> 8 & 0xFF) * wgt[k];
        b += (p[k] & 0xFF) * wgt[k];
    }
    return ((r >> 16) << 16) | ((g >> 16) << 8) | (b >> 16);
}

static void icon_draw_scaled(int app, i32 x, i32 y, i32 size)
{
    u32 *fbuf = fb_buffer();
    u32 stride = fb_width();
    if (size < 2) return;
    for (i32 j = 0; j < size; j++) {
        i32 Y = y + j;
        if (Y < 0 || Y >= (i32)fb_height()) continue;
        i32 fy = j * (SRC_S - 1) * 256 / (size - 1);
        for (i32 i = 0; i < size; i++) {
            i32 X = x + i;
            if (X < 0 || X >= (i32)stride) continue;
            i32 fx = i * (SRC_S - 1) * 256 / (size - 1);
            u32 al = bil(lp_pm[app], app, 0, fx, fy, 1, lp_al[app]);
            if (al == 0) continue;
            u32 c = bil(lp_pm[app], app, 0, fx, fy, 0, lp_al[app]);
            u32 d = fbuf[Y * stride + X], inv = 255 - al;
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

static void lp_draw_icon(int app, i32 x, i32 y)
{
    icon_draw_scaled(app, x, y, LP_ICON);
}

/* Значки пересобираются только при смене темы или акцента. */
static void icons_ensure(void)
{
    int key = (theme_is_dark() ? 16 : 0) + theme_accent_index();
    if (key == icon_key)
        return;
    lp_build_icons();
    icon_key = key;
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
    if (!lp_open)
        return;
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    fb_blend_rect(0, 0, W, H, RGB(18, 16, 40), 120);          /* затемнение вместо размытия */

    i32 sw = 260, sx = W / 2 - sw / 2, sy = 34;
    fb_fill_round_rect(sx, sy, sw, 30, 15, RGB(255, 255, 255), 38);
    lp_ring(sx + 24, sy + 13);
    if (lp_qn) {
        i32 tx = font_draw(sx + 46, sy + 6, lp_q, RGB(255, 255, 255), FONT_REGULAR);
        fb_fill_rect(tx + 2, sy + 7, 1, 16, RGB(255, 255, 255));
    } else {
        font_draw(sx + 46, sy + 6, "Search", RGB(200, 200, 215), FONT_REGULAR);
    }

    icons_ensure();
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

/* ---------- содержимое окон ---------- */

static void draw_terminal(i32 ox, i32 oy, int focused)
{
    if (cm_on) {
        cm_draw(ox, oy, app_w[APP_TERMINAL], app_h[APP_TERMINAL] - TITLE_H);
        return;
    }
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

static int window_at(i32 x, i32 y);

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

    /* Тень как в macOS: широкая и мягкая, у неактивного окна слабее. Рамки с k <= 0 лежат под окном. */
    for (i32 k = -7; k <= 28; k++) {
        i32 f = 35 - (k + 7);                       /* 35 у кромки ... 0 далеко */
        i32 a = (focused ? 96 : 50) * f * f / (35 * 35);
        if (a <= 0) continue;
        i32 sy = w->y + 10;
        fb_blend_rect(w->x - k, sy - k, w->w + 2 * k, 1, RGB(0, 0, 0), (u32)a);
        fb_blend_rect(w->x - k, sy + w->h + k - 1, w->w + 2 * k, 1, RGB(0, 0, 0), (u32)a);
        fb_blend_rect(w->x - k, sy - k + 1, 1, w->h + 2 * k - 2, RGB(0, 0, 0), (u32)a);
        fb_blend_rect(w->x + w->w + k - 1, sy - k + 1, 1, w->h + 2 * k - 2, RGB(0, 0, 0), (u32)a);
    }
    fb_fill_round_rect(w->x - 1, w->y - 1, w->w + 2, w->h + 2, WIN_R + 1, RGB(0, 0, 0), focused ? 70 : 45);   /* тонкая обводка */

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

    fb_blend_rect(w->x + WIN_R, w->y + 1, w->w - 2 * WIN_R, 1, RGB(255, 255, 255), chrome_dark ? 22 : 130);   /* блик сверху */

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

    i32 ox, oy, cw = app_w[app], ch = app_h[app] - TITLE_H;
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

/* Курсор рисуется прямо в кадр, поэтому его движение не требует перерисовки сцены. */
static void draw_cursor(void)
{
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
    int cm = 0;
    if (cm_on && wins[APP_TERMINAL].open && !wins[APP_TERMINAL].minimized && (i32)(ticks - cm_last) >= 6) {
        cm_last = ticks;
        cm_step();
        cm = 1;
    }
    return snake_tick(ticks, active) | cm;
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
    i32 W = (i32)fb_width();
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
    int app = window_at(cur_x, cur_y);
    if (app < 0)
        return;

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
    if (cx < 0 || cy < 0 || cx >= app_w[app] || cy >= app_h[app] - TITLE_H)
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
        } else if ((btn & 1) && cc_dragging()) {
            cc_drag((i32)fb_width(), cur_x);
        }
        if (released) {
            drag_app = -1;
            cc_release();
        }
    } else if (e->type == EV_KEY) {
        scene_dirty = 1;
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

void wm_render(void)
{
    fb_restore_background();
    int top = top_app();
    for (int i = 0; i < nz; i++)
        if (!lp_open && !wins[zorder[i]].minimized)
            draw_window(zorder[i], zorder[i] == top);
    draw_dock();
    if (cc_brightness() < 100)                      /* яркость: затемнение всего экрана */
        fb_blend_rect(0, 0, (i32)fb_width(), (i32)fb_height(), 0, (u32)(100 - cc_brightness()) * 2);
    draw_menubar();
    cc_draw((i32)fb_width());
    draw_menu_dropdown();
    draw_launchpad();
    scene_dirty = 0;
    cursor_moved = 0;
}

int wm_get_option(int id)
{
    return id == WM_OPT_DOCK_MAGNIFY ? dock_magnify : clock24;
}

void wm_set_option(int id, int v)
{
    if (id == WM_OPT_DOCK_MAGNIFY) dock_magnify = v ? 1 : 0;
    else                           clock24 = v ? 1 : 0;
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
        fb_frame_restore(drawn_x, drawn_y, CURSOR_W, CURSOR_H);
        draw_cursor();
        return 1;
    }
    return 0;
}
