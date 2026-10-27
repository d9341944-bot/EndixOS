/* Рабочий стол: значки файлов и контекстное меню. */
#include "desktop.h"
#include "icons.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "anim.h"
#include "widgets.h"
#include "../kernel/fs.h"

#define CELL_W 92
#define CELL_H 96
#define ICON_PX 56
#define MBAR 24
#define MAXI 48

static int sel_node = -1;
static int last_node = -1;
static u32 last_t;

static int desk_node(void) { return fs_resolve(fs_home(), "Desktop"); }

/* Раскладка: колонками сверху вниз, начиная с правого верхнего угла. */
static void cell_pos(int k, i32 W, i32 H, i32 *x, i32 *y)
{
    i32 rows = (H - MBAR - 150) / CELL_H;
    if (rows < 1) rows = 1;
    *x = W - 8 - CELL_W - (k / rows) * CELL_W;
    *y = MBAR + 10 + (k % rows) * CELL_H;
}

static int items(int *out)
{
    int d = desk_node();
    return d < 0 ? 0 : fs_list(d, out, MAXI);
}

static void label(i32 cx, i32 y, int node, int selected)
{
    char buf[48];
    const char *nm = fs_name(node);
    int n = 0;
    while (nm[n] && n < 40) { buf[n] = nm[n]; n++; }
    buf[n] = 0;
    i32 maxw = CELL_W - 10;
    if (font_width(buf, FONT_REGULAR) > maxw) {
        while (n > 1 && font_width(buf, FONT_REGULAR) + font_width("...", FONT_REGULAR) > maxw) {
            n--;
            while (n > 1 && ((u8)buf[n] & 0xC0) == 0x80) n--;
            buf[n] = 0;
        }
        buf[n] = '.'; buf[n + 1] = '.'; buf[n + 2] = '.'; buf[n + 3] = 0;
    }
    i32 tw = font_width(buf, FONT_REGULAR);
    if (selected) {
        fb_fill_round_rect(cx - tw / 2 - 6, y - 2, tw + 12, 20, 7, theme_accent(), 255);
        font_draw(cx - tw / 2, y, buf, RGB(255, 255, 255), FONT_REGULAR);
    } else {
        font_draw(cx - tw / 2 + 1, y + 1, buf, RGB(0, 0, 0), FONT_REGULAR);
        font_draw(cx - tw / 2, y, buf, RGB(255, 255, 255), FONT_REGULAR);
    }
}

void desktop_draw(i32 W, i32 H)
{
    int list[MAXI];
    int n = items(list);
    for (int k = 0; k < n; k++) {
        i32 x, y;
        cell_pos(k, W, H, &x, &y);
        i32 cx = x + CELL_W / 2;
        int s = list[k] == sel_node;
        if (s)
            fb_fill_round_rect(cx - ICON_PX / 2 - 6, y - 2, ICON_PX + 12, ICON_PX + 8, 10, RGB(255, 255, 255), 60);
        icon_draw(fs_is_dir(list[k]) ? ICON_DESK_FOLDER : ICON_DESK_FILE, cx - ICON_PX / 2, y + 2, ICON_PX);
        label(cx, y + ICON_PX + 8, list[k], s);
    }
}

int desktop_press(i32 px, i32 py, u32 ticks)
{
    int list[MAXI];
    int n = items(list);
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    for (int k = 0; k < n; k++) {
        i32 x, y;
        cell_pos(k, W, H, &x, &y);
        if (px >= x + 6 && px < x + CELL_W - 6 && py >= y && py < y + ICON_PX + 28) {
            int dbl = list[k] == last_node && (i32)(ticks - last_t) < 45;
            sel_node = list[k];
            last_node = dbl ? -1 : list[k];
            last_t = ticks;
            return dbl ? list[k] : -1;
        }
    }
    sel_node = -1;
    last_node = -1;
    return -1;
}

void desktop_deselect(void) { sel_node = -1; }

/* ---------- контекстное меню ---------- */

#define CM_W 220
#define CM_PAD 6
#define CM_ROW 26
#define CM_MAX 6
static const char *cm_text[CM_MAX];
static int cm_act[CM_MAX], cm_n, cm_sep, cm_widx = -1;       /* cm_sep: бит i - разделитель над пунктом i */
static int cm_open, cm_v, cm_x, cm_y, cm_hover = -1;

static int nsep(int upto) { int c = 0; for (int k = 1; k <= upto; k++) if (cm_sep >> k & 1) c++; return c; }
static i32 cm_h(void) { return CM_PAD * 2 + cm_n * CM_ROW + 8 * nsep(cm_n - 1); }
static i32 row_y(int i) { return cm_y + CM_PAD + i * CM_ROW + 8 * nsep(i); }
int desktop_ctx_widget(void) { return cm_widx; }

static void set_items(int widget)
{
    cm_n = 0;
    if (!widget) {
        static const char *const tx[5] = { "New Folder", "Change Wallpaper...", "Appearance...", "Liquid Glass...", "Edit Widgets..." };
        static const int ac[5] = { DK_ACT_NEWFOLDER, DK_ACT_WALLPAPER, DK_ACT_APPEAR, DK_ACT_GLASS, DK_ACT_EDITWIDGETS };
        for (int i = 0; i < 5; i++) { cm_text[i] = tx[i]; cm_act[i] = ac[i]; }
        cm_n = 5;
        cm_sep = (1 << 1) | (1 << 4);
    } else {
        cm_text[0] = "Remove Widget"; cm_act[0] = DK_ACT_REMOVEWIDGET;
        cm_text[1] = "Edit Widgets..."; cm_act[1] = DK_ACT_EDITWIDGETS;
        cm_n = 2;
        cm_sep = 1 << 1;
    }
}

static void ctx_open(i32 x, i32 y, int widget, int widx)
{
    set_items(widget);
    cm_widx = widx;
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    if (x + CM_W > W - 6) x = W - 6 - CM_W;
    if (y + cm_h() > H - 6) y = H - 6 - cm_h();
    cm_x = x; cm_y = y;
    cm_open = 1;
    cm_hover = -1;
    if (cm_v >= 1024) cm_v = 700;                       /* повторное открытие: короткая вспышка */
}

void desktop_ctx_open(i32 x, i32 y) { ctx_open(x, y, 0, -1); }
void desktop_ctx_open_widget(i32 x, i32 y, int idx) { ctx_open(x, y, 1, idx); }

void desktop_ctx_close(void) { cm_open = 0; cm_hover = -1; }
int  desktop_ctx_visible(void) { return cm_open || cm_v > 0; }
int  desktop_ctx_is_open(void) { return cm_open; }

int desktop_ctx_hit(i32 x, i32 y)
{
    return cm_open && x >= cm_x && x < cm_x + CM_W && y >= cm_y && y < cm_y + cm_h();
}

static int row_at(i32 x, i32 y)
{
    if (!desktop_ctx_hit(x, y)) return -1;
    for (int i = 0; i < cm_n; i++)
        if (y >= row_y(i) && y < row_y(i) + CM_ROW) return i;
    return -1;
}

void desktop_ctx_hover(i32 x, i32 y) { cm_hover = cm_open ? row_at(x, y) : -1; }

int desktop_ctx_click(i32 x, i32 y)
{
    int r = row_at(x, y);
    int act = r >= 0 ? cm_act[r] : DK_ACT_NONE;
    if (r >= 0 || !desktop_ctx_hit(x, y)) desktop_ctx_close();
    return act;
}

void desktop_ctx_draw(void)
{
    if (cm_v <= 0) return;
    const theme_t *t = theme();
    i32 h = cm_h();
    anim_region_begin(cm_x - 30, cm_y - 24, CM_W + 60, h + 70);
    fb_glass(cm_x, cm_y, CM_W, h, 12, GLASS_ALL, 10, theme_is_dark() ? RGB(44, 44, 50) : RGB(244, 244, 247),
             theme_is_dark() ? 150 : 165, 24);
    for (int i = 0; i < cm_n; i++) {
        i32 y = row_y(i);
        if (cm_sep >> i & 1) fb_blend_rect(cm_x + 14, y - 5, CM_W - 28, 1, RGB(0, 0, 0), t->line_alpha);
        int hv = i == cm_hover;
        if (hv) fb_fill_round_rect(cm_x + CM_PAD, y, CM_W - 2 * CM_PAD, CM_ROW, 6, theme_accent(), 255);
        font_draw(cm_x + 18, y + 4, cm_text[i], hv ? RGB(255, 255, 255) : t->text, FONT_REGULAR);
    }
    anim_region_end(-(1024 - cm_v) * 6 / 1024, cm_v, 0);   /* чуть выезжает снизу вверх и проявляется */
}

int desktop_tick(u32 dt)
{
    i32 n = anim_approach(cm_v, cm_open ? 1024 : 0, dt);
    int moving = n != cm_v || (n != 0 && n != 1024);
    cm_v = n;
    return moving;
}
