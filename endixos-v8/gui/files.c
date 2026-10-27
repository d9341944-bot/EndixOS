/* Приложение Files: просмотр и правка файловой системы в памяти. */
#include "files.h"
#include "icons.h"
#include "fb.h"
#include "ui.h"
#include "font.h"
#include "theme.h"
#include "../kernel/fs.h"
#include "../kernel/event.h"

#define SIDE_W    150
#define PREV_W    200
#define TOOL_H    38
#define ACT_H     34
#define STATUS_H  22
#define CELL_W    84
#define CELL_H    92
#define WIN_RADIUS 10
#define PREV_COLS 26
#define PREV_ROWS 10
#define DOUBLE_CLICK_TICKS 40

/* ---------- состояние ---------- */

static i32  fw_ = 720, fh_ = 416;     /* текущий размер области содержимого окна */
static int  cur = -1;                  /* открытая папка */
static int  sel = -1;                  /* выбранный узел */
static int  scroll;                    /* первая видимая строка сетки */
static int  hist[16], hpos, hlen;      /* история переходов для Back/Forward */
static int  items[FS_MAX_NODES], nitems;

static int  renaming, rlen, rfresh;    /* rfresh: текст выделен целиком, первая буква заменит его */
static char rbuf[FS_NAME_MAX];
static char msg[48];

static u32  last_tick;
static int  last_node = -2;

static const struct { const char *label; const char *path; u32 color; } side[] = {
    { "Home",      "~",           RGB(52, 120, 246) },
    { "Desktop",   "~/Desktop",   RGB(142, 90, 230) },
    { "Documents", "~/Documents", RGB(255, 149, 0)  },
    { "Downloads", "~/Downloads", RGB(40, 190, 100) },
    { "Pictures",  "~/Pictures",  RGB(255, 70, 120) },
    { "Music",     "~/Music",     RGB(230, 60, 60)  },
    { "EndixOS",   "/",           RGB(110, 115, 125) },
};
#define NSIDE ((int)(sizeof(side) / sizeof(side[0])))
static const u8 side_glyph[] = { ICON_SIDE_HOME, ICON_SIDE_DESKTOP, ICON_SIDE_DOC, ICON_SIDE_DOWN, ICON_SIDE_PHOTO, ICON_SIDE_MUSIC, ICON_SIDE_DISK };
#define SIDE_LOCATIONS 6           /* с этого пункта начинается раздел Locations */

/* ---------- строки ---------- */

static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}

static char *put_num(char *p, u32 v)
{
    char tmp[11];
    int n = 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v) {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n)
        *p++ = tmp[--n];
    *p = 0;
    return p;
}

static void set_msg(const char *s)
{
    int i = 0;
    while (s[i] && i < (int)sizeof(msg) - 1) {
        msg[i] = s[i];
        i++;
    }
    msg[i] = 0;
}

/* Укорачивает имя с многоточием, чтобы оно влезло в maxw пикселей. */
static void fit_text(char *out, const char *s, i32 maxw)
{
    int n = 0;
    while (s[n] && n < FS_NAME_MAX - 1) {
        out[n] = s[n];
        n++;
    }
    out[n] = 0;
    if (font_width(out, FONT_REGULAR) <= maxw)
        return;
    while (n > 1) {
        n--;
        out[n] = '.';
        out[n + 1] = '.';
        out[n + 2] = '.';
        out[n + 3] = 0;
        if (font_width(out, FONT_REGULAR) <= maxw)
            return;
        out[n] = 0;
    }
}

static void describe_size(char *buf, u32 sz)
{
    if (sz < 1024) {
        char *p = put_num(buf, sz);
        put_str(p, sz == 1 ? " byte" : " bytes");
    } else {
        u32 v = sz * 10 / 1024;
        char *p = put_num(buf, v / 10);
        *p++ = '.';
        p = put_num(p, v % 10);
        put_str(p, " KB");
    }
}

static const char *kind_of(int node)
{
    if (fs_is_dir(node))
        return "Folder";
    const char *n = fs_name(node);
    int l = 0;
    while (n[l])
        l++;
    if (l > 4 && n[l - 4] == '.' && n[l - 3] == 't' && n[l - 2] == 'x' && n[l - 1] == 't')
        return "Text document";
    return "Document";
}

/* ---------- геометрия ---------- */

#define MAIN_W (fw_ - SIDE_W - PREV_W)

static int ncols(void) { int c = MAIN_W / CELL_W; return c < 1 ? 1 : c; }
static int nrows(void) { int r = (fh_ - TOOL_H - ACT_H - STATUS_H - 8) / CELL_H; return r < 1 ? 1 : r; }
static i32 grid_x(void) { return SIDE_W + (MAIN_W - ncols() * CELL_W) / 2; }
static i32 grid_y(void) { return TOOL_H + ACT_H + 6; }

static int in_rect(i32 px, i32 py, i32 x, i32 y, i32 w, i32 h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

enum { B_FOLDER, B_FILE, B_RENAME, B_DELETE, B_COUNT };
static const char *const btn_label[B_COUNT] = { "New Folder", "New File", "Rename", "Delete" };
static const i32 btn_w[B_COUNT] = { 96, 80, 72, 68 };

static i32 btn_x(int i)
{
    i32 x = SIDE_W + 12;
    for (int k = 0; k < i; k++)
        x += btn_w[k] + 8;
    return x;
}

#define BTN_Y (TOOL_H + 5)
#define BACK_X (SIDE_W + 12)
#define FWD_X  (SIDE_W + 44)

/* ---------- навигация и выбор ---------- */

static void refresh(void)
{
    if (cur < 0 || !fs_is_dir(cur))        /* папку удалили, например из терминала */
        cur = fs_home();
    nitems = fs_list(cur, items, FS_MAX_NODES);
    if (sel >= 0) {
        int found = 0;
        for (int i = 0; i < nitems; i++)
            if (items[i] == sel)
                found = 1;
        if (!found) {
            sel = -1;
            renaming = 0;
        }
    }
}

static int index_of(int node)
{
    for (int i = 0; i < nitems; i++)
        if (items[i] == node)
            return i;
    return -1;
}

static void ensure_visible(void)
{
    int idx = index_of(sel);
    if (idx < 0)
        return;
    int row = idx / ncols();
    if (row < scroll)
        scroll = row;
    if (row >= scroll + nrows())
        scroll = row - nrows() + 1;
}

static void rename_end(int commit);

static void go(int dir)
{
    if (!fs_is_dir(dir) || dir == cur)
        return;
    rename_end(1);
    hlen = hpos + 1;                       /* переход обрезает историю "вперёд" */
    if (hlen == 16) {
        for (int i = 1; i < 16; i++)
            hist[i - 1] = hist[i];
        hlen--;
        hpos--;
    }
    hist[hlen++] = dir;
    hpos = hlen - 1;
    cur = dir;
    sel = -1;
    scroll = 0;
    msg[0] = 0;
    refresh();
}

static void go_history(int delta)
{
    int np = hpos + delta;
    if (np < 0 || np >= hlen)
        return;
    rename_end(1);
    hpos = np;
    cur = fs_is_dir(hist[hpos]) ? hist[hpos] : fs_home();
    sel = -1;
    scroll = 0;
    msg[0] = 0;
    refresh();
}

static void select_index(int idx)
{
    if (idx < 0 || idx >= nitems)
        return;
    sel = items[idx];
    msg[0] = 0;
    ensure_visible();
}

/* ---------- переименование ---------- */

static void rename_begin(void)
{
    if (sel < 0 || !fs_valid(sel))
        return;
    const char *n = fs_name(sel);
    rlen = 0;
    while (n[rlen] && rlen < FS_NAME_MAX - 1) {
        rbuf[rlen] = n[rlen];
        rlen++;
    }
    rbuf[rlen] = 0;
    renaming = 1;
    rfresh = 1;
    msg[0] = 0;
}

static void rename_end(int commit)
{
    if (!renaming)
        return;
    renaming = 0;
    if (!commit || rlen == 0 || sel < 0)
        return;
    rbuf[rlen] = 0;
    int rc = fs_move(sel, cur, rbuf);
    if (rc == FS_EEXIST) {
        set_msg("That name is already taken");
        renaming = 1;                      /* оставляем поле, пусть исправит */
        rfresh = 0;
    } else if (rc != 0) {
        set_msg("Invalid name");
        renaming = 1;
        rfresh = 0;
    } else {
        refresh();
        ensure_visible();
    }
}

/* ---------- действия ---------- */

static void make_new(int is_dir)
{
    char name[FS_NAME_MAX];
    rename_end(1);
    if (renaming)
        return;
    fs_unique_name(cur, is_dir ? "untitled folder" : "untitled.txt", name);
    int id = is_dir ? fs_mkdir(cur, name) : fs_create(cur, name);
    if (id < 0) {
        set_msg("Not enough space");
        return;
    }
    refresh();
    sel = id;
    ensure_visible();
    rename_begin();
}

static void delete_selected(void)
{
    if (sel < 0 || !fs_valid(sel))
        return;
    char buf[48], *p;
    int idx = index_of(sel);
    p = put_str(buf, "Deleted ");
    int l = 0;
    const char *n = fs_name(sel);
    while (n[l] && l < 30)
        *p++ = n[l++];
    *p = 0;

    renaming = 0;
    if (fs_remove_tree(sel) != 0) {
        set_msg("Cannot delete this item");
        return;
    }
    refresh();
    sel = -1;
    if (nitems > 0)
        select_index(idx < nitems ? idx : nitems - 1);
    set_msg(buf);
}

/* ---------- иконки ---------- */

#define S(n) ((n) * s / 48)

static void draw_icon(int is_dir, i32 x, i32 y, i32 s)
{
    if (is_dir) {
        u32 back = RGB(58, 128, 228), front = RGB(92, 170, 250);
        fb_fill_round_rect(x + S(3), y + S(7), S(20), S(12), S(4), back, 255);
        fb_fill_round_rect(x + S(3), y + S(11), S(42), S(30), S(5), back, 255);
        fb_fill_round_rect(x + S(3), y + S(17), S(42), S(26), S(5), front, 255);
        fb_blend_rect(x + S(5), y + S(18), S(38), 1, RGB(255, 255, 255), 90);
    } else {
        fb_fill_round_rect(x + S(9), y + S(2), S(31), S(44), S(5), RGB(190, 190, 198), 255);
        fb_fill_round_rect(x + S(10), y + S(3), S(29), S(42), S(4), RGB(255, 255, 255), 255);
        fb_fill_rect(x + S(30), y + S(3), S(9), S(9), RGB(222, 222, 228));      /* загнутый угол */
        for (int i = 0; i < 5; i++)
            fb_fill_rect(x + S(14), y + S(17 + i * 5), S(i == 4 ? 12 : 21), 2, RGB(170, 170, 180));
    }
}

/* ---------- отрисовка ---------- */

static void button(i32 x, i32 y, i32 w, const char *label, int enabled)
{
    ui_button(x, y, w, 24, label, enabled, 0);
}

static void draw_sidebar(i32 x, i32 y)
{
    const theme_t *t = theme();

    /* фон боковой панели - стекло, его рисует оболочка окон */
    fb_blend_rect(x + SIDE_W - 1, y, 1, fh_, RGB(0, 0, 0), t->line_alpha);

    font_draw(x + 18, y + 12, "Favorites", t->sub, FONT_BOLD);
    for (int i = 0; i < NSIDE; i++) {
        i32 ix = x + 10;
        i32 iy = y + 34 + i * 30 + (i >= SIDE_LOCATIONS ? 28 : 0);
        int node = fs_resolve(0, side[i].path);
        int active = (node >= 0 && node == cur);

        if (i == SIDE_LOCATIONS)
            font_draw(x + 18, iy - 24, "Locations", t->sub, FONT_BOLD);
        if (active)
            fb_fill_round_rect(ix, iy, SIDE_W - 20, 26, 7, theme_is_dark() ? RGB(255, 255, 255) : RGB(0, 0, 0),
                               theme_is_dark() ? 30 : 22);
        icon_draw(side_glyph[i], ix + 6, iy + 3, 20);
        font_draw(ix + 32, iy + 4, side[i].label, node >= 0 ? t->text : t->sub, FONT_REGULAR);
    }
}

static void draw_toolbar(i32 x, i32 y)
{
    const theme_t *t = theme();
    char path[96];

    /* Back / Forward: одна сегментная кнопка */
    ui_button(x + BACK_X, y + 7, 58, 24, "", 1, 0);
    fb_blend_rect(x + BACK_X + 29, y + 9, 1, 20, RGB(0, 0, 0), 40);
    ui_chevron(x + BACK_X + 13, y + 19, -1, hpos > 0 ? t->text : t->sub);
    ui_chevron(x + BACK_X + 44, y + 19, 1, hpos < hlen - 1 ? t->text : t->sub);

    fs_path(cur, path, sizeof(path));
    const char *title = cur == fs_root() ? "EndixOS" : fs_name(cur);
    i32 tx = font_draw(x + SIDE_W + 84, y + 10, title, t->text, FONT_BOLD);
    font_draw(tx + 12, y + 11, path, t->sub, FONT_REGULAR);

    for (int i = 0; i < B_COUNT; i++) {
        int enabled = (i < B_RENAME) || sel >= 0;
        button(x + btn_x(i), y + BTN_Y + 0, btn_w[i], btn_label[i], enabled);
    }
    fb_blend_rect(x + SIDE_W, y + TOOL_H + ACT_H - 1, MAIN_W + PREV_W, 1, RGB(0, 0, 0), t->line_alpha);
}

static void draw_grid(i32 x, i32 y)
{
    const theme_t *t = theme();
    int cols = ncols(), rows = nrows();
    i32 gx = x + grid_x(), gy = y + grid_y();

    if (nitems == 0) {
        const char *s = "This folder is empty";
        font_draw(x + SIDE_W + (MAIN_W - font_width(s, FONT_REGULAR)) / 2, gy + 60, s, t->sub, FONT_REGULAR);
    }

    for (int i = scroll * cols; i < nitems && i < (scroll + rows) * cols; i++) {
        int c = i % cols, r = i / cols - scroll;
        i32 cx = gx + c * CELL_W, cy = gy + r * CELL_H;
        int node = items[i];

        if (node == sel)
            fb_fill_round_rect(cx + 8, cy + 4, CELL_W - 16, 56, 8, theme_is_dark() ? RGB(255, 255, 255) : RGB(0, 0, 0),
                               theme_is_dark() ? 34 : 24);
        draw_icon(fs_is_dir(node), cx + (CELL_W - 48) / 2, cy + 8, 48);

        if (renaming && node == sel) {
            i32 bx = cx + 3, by = cy + 62, bw = CELL_W - 6;
            const char *vis = rbuf;
            while (font_width(vis, FONT_REGULAR) > bw - 10 && *vis)    /* показываем хвост строки */
                vis++;
            i32 tw = font_width(vis, FONT_REGULAR);
            fb_fill_round_rect(bx - 1, by - 1, bw + 2, 22, 6, theme_accent(), 255);
            fb_fill_round_rect(bx, by, bw, 20, 5, t->card, 255);
            if (rfresh) {
                fb_fill_rect(bx + 4, by + 2, tw + 2, 16, theme_accent());
                font_draw(bx + 5, by + 2, vis, RGB(255, 255, 255), FONT_REGULAR);
            } else {
                font_draw(bx + 5, by + 2, vis, t->text, FONT_REGULAR);
                fb_fill_rect(bx + 6 + tw, by + 3, 1, 14, t->text);
            }
        } else {
            char label[FS_NAME_MAX + 4];
            fit_text(label, fs_name(node), CELL_W - 8);
            i32 lw = font_width(label, FONT_REGULAR);
            if (node == sel)
                fb_fill_round_rect(cx + (CELL_W - lw) / 2 - 5, cy + 63, lw + 10, 20, 5, theme_accent(), 255);
            font_draw(cx + (CELL_W - lw) / 2, cy + 64, label,
                      node == sel ? RGB(255, 255, 255) : t->text, FONT_REGULAR);
        }
    }

    /* полоса прокрутки, если элементов больше, чем помещается */
    int total_rows = (nitems + cols - 1) / cols;
    if (total_rows > rows) {
        i32 th = rows * CELL_H, ty = gy;
        i32 thumb = th * rows / total_rows;
        if (thumb < 24)
            thumb = 24;
        i32 pos = (th - thumb) * scroll / (total_rows - rows);
        fb_fill_round_rect(x + SIDE_W + MAIN_W - 8, ty + pos, 4, thumb, 2, RGB(0, 0, 0), 80);
    }
}

static void draw_status(i32 x, i32 y)
{
    const theme_t *t = theme();
    char buf[64], *p;
    i32 sy = y + fh_ - STATUS_H;

    fb_blend_rect(x + SIDE_W, sy, MAIN_W, 1, RGB(0, 0, 0), t->line_alpha);
    if (msg[0]) {
        p = put_str(buf, msg);
    } else if (nitems == 0) {
        p = put_str(buf, "Empty");
    } else {
        p = put_num(buf, (u32)nitems);
        p = put_str(p, nitems == 1 ? " item" : " items");
    }
    font_draw(x + SIDE_W + 12, sy + 3, buf, t->sub, FONT_REGULAR);
}

static void draw_preview(i32 x, i32 y)
{
    const theme_t *t = theme();
    i32 px = x + fw_ - PREV_W;
    char buf[64], *p;

    /* фон панели; нижний правый угол повторяет скругление окна */
    fb_fill_round_rect(px - WIN_RADIUS, y + fh_ - 2 * WIN_RADIUS, PREV_W + WIN_RADIUS,
                       2 * WIN_RADIUS, WIN_RADIUS, t->side, 255);
    fb_fill_rect(px - WIN_RADIUS, y + fh_ - 2 * WIN_RADIUS, WIN_RADIUS, 2 * WIN_RADIUS, t->body);
    fb_fill_rect(px, y, PREV_W, fh_ - WIN_RADIUS, t->side);
    fb_blend_rect(px, y, 1, fh_, RGB(0, 0, 0), t->line_alpha);

    int node = sel >= 0 ? sel : cur;
    int is_dir = fs_is_dir(node);
    draw_icon(is_dir, px + (PREV_W - 64) / 2, y + 16, 64);

    char name[FS_NAME_MAX + 4];
    const char *nm = node == fs_root() ? "EndixOS" : fs_name(node);
    fit_text(name, nm, PREV_W - 24);
    font_draw(px + (PREV_W - font_width(name, FONT_BOLD)) / 2, y + 90, name, t->text, FONT_BOLD);

    font_draw(px + (PREV_W - font_width(kind_of(node), FONT_REGULAR)) / 2, y + 110,
              kind_of(node), t->sub, FONT_REGULAR);
    if (is_dir) {
        int n = fs_count(node);
        p = put_num(buf, (u32)n);
        put_str(p, n == 1 ? " item" : " items");
    } else {
        describe_size(buf, fs_size(node));
    }
    font_draw(px + (PREV_W - font_width(buf, FONT_REGULAR)) / 2, y + 128, buf, t->sub, FONT_REGULAR);

    if (!is_dir) {                                   /* начало текста файла */
        const u8 *d = fs_data(node);
        u32 sz = fs_size(node), i;
        int line = 0, col = 0;
        char ch[2] = { 0, 0 };

        fb_blend_rect(px + 12, y + 152, PREV_W - 24, 1, RGB(0, 0, 0), t->line_alpha);
        for (i = 0; i < sz && line < PREV_ROWS; i++) {
            u8 c = d[i];
            if (c == '\n') {
                line++;
                col = 0;
                continue;
            }
            if (c < 32 || c > 126)
                c = '.';
            if (col >= PREV_COLS) {
                line++;
                col = 0;
                if (line >= PREV_ROWS)
                    break;
            }
            ch[0] = (char)c;
            font_draw(px + 10 + col * FONT_MONO_ADV, y + 160 + line * 15, ch, t->text, FONT_MONO);
            col++;
        }
        if (i < sz)
            font_draw(px + 10, y + 160 + PREV_ROWS * 15, "...", t->sub, FONT_MONO);
    }
}

void files_draw(i32 x, i32 y)
{
    refresh();
    draw_sidebar(x, y);
    draw_preview(x, y);
    draw_toolbar(x, y);
    draw_grid(x, y);
    draw_status(x, y);
}

/* ---------- ввод ---------- */

void files_goto(int node)
{
    if (!fs_valid(node)) return;
    int d = fs_is_dir(node) ? node : fs_parent(node);
    if (d < 0) return;
    if (d != cur) go(d);
    sel = fs_is_dir(node) ? -1 : node;
}

void files_init(void)
{
    cur = fs_home();
    hist[0] = cur;
    hpos = 0;
    hlen = 1;
    sel = -1;
    scroll = 0;
    renaming = 0;
    msg[0] = 0;
    last_node = -2;
    refresh();
}

static int grid_item_at(i32 rx, i32 ry)
{
    i32 gx = grid_x(), gy = grid_y();
    if (rx < gx || ry < gy)
        return -1;
    int c = (rx - gx) / CELL_W, r = (ry - gy) / CELL_H;
    if (c >= ncols() || r >= nrows())
        return -1;
    int idx = (scroll + r) * ncols() + c;
    return idx < nitems ? items[idx] : -1;
}

int files_click(i32 rx, i32 ry, u32 ticks)
{
    refresh();

    if (rx < SIDE_W) {                                   /* боковая панель */
        for (int i = 0; i < NSIDE; i++) {
            i32 iy = 34 + i * 30 + (i >= SIDE_LOCATIONS ? 28 : 0);
            if (in_rect(rx, ry, 10, iy, SIDE_W - 20, 26)) {
                int node = fs_resolve(0, side[i].path);
                if (node >= 0)
                    go(node);
                return 1;
            }
        }
        return 0;
    }

    if (ry < TOOL_H) {                                   /* Back / Forward */
        if (in_rect(rx, ry, BACK_X, 7, 26, 24)) { go_history(-1); return 1; }
        if (in_rect(rx, ry, FWD_X, 7, 26, 24))  { go_history(1);  return 1; }
        return 0;
    }

    if (ry < TOOL_H + ACT_H) {                           /* кнопки действий */
        for (int i = 0; i < B_COUNT; i++) {
            if (!in_rect(rx, ry, btn_x(i), BTN_Y, btn_w[i], 24))
                continue;
            if (i == B_FOLDER)      make_new(1);
            else if (i == B_FILE)   make_new(0);
            else if (i == B_RENAME) { rename_end(1); rename_begin(); }
            else                    delete_selected();
            return 1;
        }
        return 0;
    }

    if (rx >= fw_ - PREV_W || ry >= fh_ - STATUS_H)
        return 0;

    /* сетка файлов */
    int node = grid_item_at(rx, ry);
    if (renaming && node != sel)
        rename_end(1);
    if (node < 0) {
        sel = -1;
        last_node = -2;
        msg[0] = 0;
        return 1;
    }
    if (node == last_node && (i32)(ticks - last_tick) <= DOUBLE_CLICK_TICKS) {
        last_node = -2;
        sel = node;
        msg[0] = 0;
        if (fs_is_dir(node))
            go(node);                                    /* двойной клик по папке открывает её */
        return 1;
    }
    last_node = node;
    last_tick = ticks;
    sel = node;
    msg[0] = 0;
    return 1;
}

int files_key(i32 key)
{
    refresh();

    if (renaming) {
        if (key == 27) {
            rename_end(0);
        } else if (key == '\n') {
            rename_end(1);
        } else if (key == '\b') {
            if (rfresh) {
                rlen = 0;
                rfresh = 0;
            } else if (rlen > 0) {
                rlen--;
            }
            rbuf[rlen] = 0;
        } else if (key >= 32 && key < 127 && key != '/') {
            if (rfresh) {
                rlen = 0;
                rfresh = 0;
            }
            if (rlen < FS_NAME_MAX - 1) {
                rbuf[rlen++] = (char)key;
                rbuf[rlen] = 0;
            }
            msg[0] = 0;
        }
        return 1;
    }

    int cols = ncols();
    int idx = index_of(sel);

    switch (key) {
    case KEY_LEFT:  select_index(idx < 0 ? 0 : idx - 1); return 1;
    case KEY_RIGHT: select_index(idx < 0 ? 0 : idx + 1); return 1;
    case KEY_UP:    select_index(idx < 0 ? 0 : idx - cols); return 1;
    case KEY_DOWN:  select_index(idx < 0 ? 0 : idx + cols); return 1;
    case '\n':
        if (sel >= 0 && fs_is_dir(sel))
            go(sel);
        return 1;
    case '\b':
        if (fs_parent(cur) >= 0)
            go(fs_parent(cur));
        return 1;
    case KEY_DELETE:
        delete_selected();
        return 1;
    case KEY_F2:
        rename_begin();
        return 1;
    default:
        return 0;
    }
}

void files_new_folder(void) { make_new(1); }

/* Окно изменило размер: подгоняем сетку и прокрутку. */
void files_set_size(i32 w, i32 h)
{
    fw_ = w;
    fh_ = h;
    int cols = ncols(), rows = nrows();
    if (cols < 1)
        return;
    int total = (nitems + cols - 1) / cols;
    if (scroll > total - rows)
        scroll = total - rows > 0 ? total - rows : 0;
}
