/* Приложение Settings: боковое меню, страницы Wallpapers / Themes / About. */
#include "settings.h"
#include "icons.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "logo.h"
#include "ui.h"
#include "wallpaper.h"

#define SIDEBAR_W 170
#define SIDE_ITEMS_Y 72
#define WIN_RADIUS 10
#define THUMB_W 100
#define THUMB_H 62
#define CARD_W 150
#define CARD_H 100

enum { PAGE_WALL, PAGE_THEMES, PAGE_GLASS, PAGE_ABOUT, PAGE_COUNT };

static const char *const page_name[PAGE_COUNT] = { "Wallpaper", "Appearance", "Liquid Glass", "About" };

static const sysinfo_t *si;
static int page = PAGE_WALL;
static int wall;
static u32 thumbs[WALL_COUNT][THUMB_W * THUMB_H];

/* ---------- геометрия (общая для рисования и кликов) ---------- */

static void thumb_pos(int i, i32 *x, i32 *y)
{
    *x = SIDEBAR_W + 20 + (i % 4) * (THUMB_W + 12);
    *y = 84 + (i / 4) * (THUMB_H + 34);
}

static void card_pos(int i, i32 *x, i32 *y)
{
    *x = SIDEBAR_W + 24 + i * (CARD_W + 20);
    *y = 80;
}

#define GRP_X (SIDEBAR_W + 24)
#define GRP_W 422
#define GRP_Y 216
#define ROW_H 42
#define HIDE_W 170
#define HIDE_X (GRP_X + GRP_W - 16 - HIDE_W)
#define SL_W  170
#define SL_X  (GRP_X + GRP_W - 16 - SL_W)

static int sl_drag;                     /* какой ползунок тянут: 1 - зум Dock, 2.. - параметры Liquid Glass */

static void accent_pos(int i, i32 *cx, i32 *cy)
{
    *cx = GRP_X + GRP_W - 24 - (ACCENT_COUNT - 1 - i) * 28;
    *cy = GRP_Y + ROW_H / 2;
}

static int in_rect(i32 px, i32 py, i32 x, i32 y, i32 w, i32 h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

/* ---------- публичное ---------- */

void settings_set_wallpaper(int id)
{
    if (id < 0 || id >= WALL_COUNT)
        return;
    wall = id;
    wallpaper_paint(id, fb_buffer(), fb_width(), fb_height());
    fb_save_background();
}

int settings_wallpaper(void)
{
    return wall;
}

void settings_init(const sysinfo_t *info)
{
    si = info;
    for (int i = 0; i < WALL_COUNT; i++)
        wallpaper_paint(i, thumbs[i], THUMB_W, THUMB_H);
    settings_set_wallpaper(0);
}

/* ---------- About ---------- */

static char *put_str(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

static char *put_u32(char *p, u32 v)
{
    char tmp[11];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) *p++ = tmp[--n];
    *p = 0;
    return p;
}

static void about_row(i32 x, i32 y, const char *label, const char *value)
{
    const theme_t *t = theme();
    font_draw(x, y, label, t->sub, FONT_BOLD);
    font_draw(x + 100, y, value, t->text, FONT_REGULAR);
}

void about_draw(i32 x, i32 y)
{
    const theme_t *t = theme();
    char buf[96];

    logo_draw(x + 28, y + 30, 72, theme_accent());

    font_draw(x + 124, y + 26, "EndixOS", t->text, FONT_TITLE);
    font_draw(x + 125, y + 62, "Version " ENDIX_VERSION, t->sub, FONT_REGULAR);

    i32 ry = y + 118;
    about_row(x + 28, ry, "Processor", si ? si->cpu : "?");
    char *p = put_u32(buf, si ? si->ram_mb : 0);
    put_str(p, " MB");
    about_row(x + 28, ry + 24, "Memory", buf);
    p = put_u32(buf, si ? si->width : 0);
    p = put_str(p, " x ");
    put_u32(p, si ? si->height : 0);
    about_row(x + 28, ry + 48, "Display", buf);
    about_row(x + 28, ry + 72, "Kernel", "EndixOS i686, written from scratch");
}

/* ---------- страницы ---------- */

static void heading(i32 x, i32 y, const char *title, const char *subtitle)
{
    const theme_t *t = theme();
    font_draw(x + SIDEBAR_W + 24, y + 12, title, t->text, FONT_TITLE);
    font_draw(x + SIDEBAR_W + 25, y + 50, subtitle, t->sub, FONT_REGULAR);
}

static void draw_wallpapers(i32 x, i32 y)
{
    const theme_t *t = theme();
    heading(x, y, "Wallpaper", "Click a picture to set it as the desktop background");

    for (int i = 0; i < WALL_COUNT; i++) {
        i32 tx, ty;
        thumb_pos(i, &tx, &ty);
        tx += x;
        ty += y;
        if (i == wall)
            fb_fill_round_rect(tx - 3, ty - 3, THUMB_W + 6, THUMB_H + 6, 7, theme_accent(), 255);
        else
            fb_fill_round_rect(tx - 1, ty - 1, THUMB_W + 2, THUMB_H + 2, 5, RGB(0, 0, 0), 40);
        fb_blit(tx, ty, thumbs[i], THUMB_W, THUMB_H);
        const char *name = wallpaper_name(i);
        font_draw(tx + (THUMB_W - font_width(name, FONT_REGULAR)) / 2, ty + THUMB_H + 7,
                  name, i == wall ? t->text : t->sub, FONT_REGULAR);
    }
}

static void draw_card(i32 x, i32 y, int dark, int selected, const char *label)
{
    const theme_t *t = theme();
    const theme_t *v = theme_variant(dark);
    if (selected)
        fb_fill_round_rect(x - 3, y - 3, CARD_W + 6, CARD_H + 6, 13, theme_accent(), 255);
    fb_fill_round_rect(x, y, CARD_W, CARD_H, 10, dark ? RGB(24, 24, 28) : RGB(214, 218, 230), 255);

    /* мини-окно внутри карточки */
    i32 wx = x + 20, wy = y + 18, ww = CARD_W - 40, wh = CARD_H - 34;
    fb_fill_round_rect(wx, wy, ww, wh, 6, v->body, 255);
    fb_fill_round_rect(wx, wy, ww, 16, 6, v->title_f, 255);
    fb_fill_rect(wx, wy + 8, ww, 8, v->title_f);
    fb_fill_circle(wx + 8, wy + 8, 2, RGB(255, 95, 87));
    fb_fill_circle(wx + 16, wy + 8, 2, RGB(254, 188, 46));
    fb_fill_circle(wx + 24, wy + 8, 2, RGB(40, 200, 64));
    fb_fill_round_rect(wx + 10, wy + 24, ww - 36, 5, 2, v->sub, 255);
    fb_fill_round_rect(wx + 10, wy + 35, ww - 20, 5, 2, v->side, 255);
    fb_fill_round_rect(wx + 10, wy + 46, ww - 52, 5, 2, theme_accent(), 255);

    font_draw(x + (CARD_W - font_width(label, FONT_REGULAR)) / 2, y + CARD_H + 8,
              label, selected ? t->text : t->sub, FONT_REGULAR);
}

static void draw_themes(i32 x, i32 y)
{
    const theme_t *t = theme();
    heading(x, y, "Appearance", "Light or dark windows, accent color and shell options");

    for (int i = 0; i < 2; i++) {
        i32 cx, cy;
        card_pos(i, &cx, &cy);
        draw_card(x + cx, y + cy, i, theme_is_dark() == i, i ? "Dark" : "Light");
    }

    ui_group(x + GRP_X, y + GRP_Y, GRP_W, ROW_H * 6);
    font_draw(x + GRP_X + 16, y + GRP_Y + 12, "Accent color", t->text, FONT_REGULAR);
    for (int i = 0; i < ACCENT_COUNT; i++) {
        i32 cx, cy;
        accent_pos(i, &cx, &cy);
        cx += x;
        cy += y;
        if (i == theme_accent_index()) {
            fb_fill_circle(cx, cy, 13, theme_accent_color(i));
            fb_fill_circle(cx, cy, 11, t->card);
        }
        fb_fill_circle(cx, cy, 9, theme_accent_color(i));
    }
    ui_hline(x + GRP_X + 16, y + GRP_Y + ROW_H, GRP_W - 16);
    font_draw(x + GRP_X + 16, y + GRP_Y + ROW_H + 13, "Dock magnification", t->text, FONT_REGULAR);
    ui_switch(x + GRP_X + GRP_W - 52, y + GRP_Y + ROW_H + 9, wm_get_option(WM_OPT_DOCK_MAGNIFY));
    ui_hline(x + GRP_X + 16, y + GRP_Y + 2 * ROW_H, GRP_W - 16);
    font_draw(x + GRP_X + 16, y + GRP_Y + 2 * ROW_H + 13, "Magnification size", wm_get_option(WM_OPT_DOCK_MAGNIFY) ? t->text : t->sub, FONT_REGULAR);
    ui_slider(x + SL_X, y + GRP_Y + 2 * ROW_H + ROW_H / 2, SL_W, wm_get_option(WM_OPT_DOCK_ZOOM), wm_get_option(WM_OPT_DOCK_MAGNIFY));
    ui_hline(x + GRP_X + 16, y + GRP_Y + 3 * ROW_H, GRP_W - 16);
    font_draw(x + GRP_X + 16, y + GRP_Y + 3 * ROW_H + 13, "Dock hiding", t->text, FONT_REGULAR);
    {
        static const char *const hide_labels[3] = { "Off", "Auto", "Smart" };
        ui_segment(x + HIDE_X, y + GRP_Y + 3 * ROW_H + 9, HIDE_W, hide_labels, 3, wm_get_option(WM_OPT_DOCK_HIDE));
    }
    ui_hline(x + GRP_X + 16, y + GRP_Y + 4 * ROW_H, GRP_W - 16);
    font_draw(x + GRP_X + 16, y + GRP_Y + 4 * ROW_H + 13, "24-hour time", t->text, FONT_REGULAR);
    ui_switch(x + GRP_X + GRP_W - 52, y + GRP_Y + 4 * ROW_H + 9, wm_get_option(WM_OPT_CLOCK24));
    ui_hline(x + GRP_X + 16, y + GRP_Y + 5 * ROW_H, GRP_W - 16);
    font_draw(x + GRP_X + 16, y + GRP_Y + 5 * ROW_H + 12, "Reduce transparency", t->text, FONT_REGULAR);
    ui_switch(x + GRP_X + GRP_W - 52, y + GRP_Y + 5 * ROW_H + 9, wm_get_option(WM_OPT_REDUCE_TRANSP));
}

/* ---------- Liquid Glass ---------- */

static const char *const glass_label[GLASS_PARAMS] = { "Refraction", "Blur", "Frost", "Edge light", "Vibrancy" };
#define PV_Y 84
#define PV_H 116
#define RST_Y (GRP_Y + GLASS_PARAMS * ROW_H + 12)

static void draw_glass(i32 x, i32 y)
{
    const theme_t *t = theme();
    heading(x, y, "Liquid Glass", "Adjust the glass of the Dock, menus and panels");

    /* образец: яркий фон с крупными фигурами и стеклянная панель поверх */
    i32 px = x + GRP_X, py = y + PV_Y;
    for (i32 i = 0; i < GRP_W; i++) {
        u32 r = (u32)(70 + i * 150 / GRP_W), g = (u32)(60 + (GRP_W - i) * 60 / GRP_W), b = (u32)(220 - i * 90 / GRP_W);
        fb_fill_rect(px + i, py, 1, PV_H, RGB(r, g, b));
    }
    fb_fill_circle(px + 66, py + 40, 34, RGB(255, 214, 80));
    fb_fill_circle(px + 350, py + 78, 40, RGB(60, 224, 230));
    fb_fill_circle(px + 230, py + 30, 22, RGB(255, 255, 255));
    fb_fill_rect(px + 120, py + 12, 14, PV_H - 24, RGB(255, 90, 140));
    fb_fill_rect(px + 300, py + 8, 10, PV_H - 16, RGB(20, 20, 40));
    fb_glass(px + 56, py + 26, GRP_W - 112, 64, 24, GLASS_ALL, 10, theme_is_dark() ? RGB(36, 36, 42) : RGB(238, 238, 243),
             theme_is_dark() ? 105 : 120, 30);
    {
        const char *cap = "Liquid Glass";
        font_draw(px + GRP_W / 2 - font_width(cap, FONT_BOLD) / 2, py + 26 + 22, cap, theme_is_dark() ? RGB(245, 245, 248) : RGB(30, 30, 34), FONT_BOLD);
    }

    ui_group(x + GRP_X, y + GRP_Y, GRP_W, ROW_H * GLASS_PARAMS);
    for (int i = 0; i < GLASS_PARAMS; i++) {
        i32 ry = y + GRP_Y + i * ROW_H;
        if (i) ui_hline(x + GRP_X + 16, ry, GRP_W - 16);
        font_draw(x + GRP_X + 16, ry + 13, glass_label[i], t->text, FONT_REGULAR);
        char buf[8];
        put_u32(buf, (u32)fb_glass_param_get(i));
        font_draw(x + SL_X - 40, ry + 13, buf, t->sub, FONT_REGULAR);
        ui_slider(x + SL_X, ry + ROW_H / 2, SL_W, fb_glass_param_get(i), 1);
    }
    ui_button(x + GRP_X, y + RST_Y, 120, 28, "Reset", 1, 0);
}

/* ---------- окно целиком ---------- */

void settings_draw(i32 x, i32 y, i32 w, i32 h)
{
    const theme_t *t = theme();
    (void)w;

    /* фон боковой панели - стекло, его рисует оболочка окон */
    fb_blend_rect(x + SIDEBAR_W - 1, y, 1, h, RGB(0, 0, 0), t->line_alpha);

    /* учётная запись */
    fb_fill_circle(x + 36, y + 36, 17, theme_accent());
    logo_draw(x + 36 - 10, y + 36 - 10, 20, RGB(255, 255, 255));
    font_draw(x + 62, y + 20, "endix", t->text, FONT_BOLD);
    font_draw(x + 62, y + 38, "EndixOS user", t->sub, FONT_REGULAR);

    for (int i = 0; i < PAGE_COUNT; i++) {
        i32 ix = x + 10, iy = y + SIDE_ITEMS_Y + i * 34;
        int sel = (i == page);
        if (sel)
            fb_fill_round_rect(ix, iy, SIDEBAR_W - 20, 28, 7, theme_accent(), 255);
        if (i == PAGE_ABOUT) {
            fb_fill_round_rect(ix + 6, iy + 4, 20, 20, 6, sel ? RGB(255, 255, 255) : theme_accent(), 255);
            logo_draw(ix + 9, iy + 7, 14, sel ? theme_accent() : RGB(255, 255, 255));
        } else {
            icon_draw(i == PAGE_WALL ? ICON_TILE_WALL : i == PAGE_GLASS ? ICON_TILE_GLASS : ICON_TILE_APPEAR, ix + 6, iy + 4, 20);
        }
        font_draw(ix + 36, iy + 5, page_name[i], sel ? RGB(255, 255, 255) : t->text, FONT_REGULAR);
    }

    switch (page) {
    case PAGE_WALL:   draw_wallpapers(x, y); break;
    case PAGE_THEMES: draw_themes(x, y);     break;
    case PAGE_GLASS:  draw_glass(x, y);      break;
    case PAGE_ABOUT:  about_draw(x + SIDEBAR_W, y); break;
    }
}

int settings_click(i32 rx, i32 ry)
{
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (in_rect(rx, ry, 10, SIDE_ITEMS_Y + i * 34, SIDEBAR_W - 20, 28)) {
            page = i;
            return 1;
        }
    }

    if (page == PAGE_WALL) {
        for (int i = 0; i < WALL_COUNT; i++) {
            i32 tx, ty;
            thumb_pos(i, &tx, &ty);
            if (in_rect(rx, ry, tx, ty, THUMB_W, THUMB_H)) {
                settings_set_wallpaper(i);
                return 1;
            }
        }
    } else if (page == PAGE_GLASS) {
        for (int i = 0; i < GLASS_PARAMS; i++)
            if (in_rect(rx, ry, SL_X - 12, GRP_Y + i * ROW_H, SL_W + 24, ROW_H)) {
                sl_drag = 2 + i;
                settings_drag(rx);
                return 1;
            }
        if (in_rect(rx, ry, GRP_X, RST_Y, 120, 28)) {
            fb_glass_param_reset();
            return 1;
        }
    } else if (page == PAGE_THEMES) {
        for (int i = 0; i < 2; i++) {
            i32 cx, cy;
            card_pos(i, &cx, &cy);
            if (in_rect(rx, ry, cx, cy, CARD_W, CARD_H)) {
                theme_set_dark(i);
                return 1;
            }
        }
        if (in_rect(rx, ry, GRP_X, GRP_Y + ROW_H, GRP_W, ROW_H)) {
            wm_set_option(WM_OPT_DOCK_MAGNIFY, !wm_get_option(WM_OPT_DOCK_MAGNIFY));
            return 1;
        }
        if (in_rect(rx, ry, SL_X - 12, GRP_Y + 2 * ROW_H, SL_W + 24, ROW_H)) {
            sl_drag = 1;
            settings_drag(rx);
            return 1;
        }
        if (in_rect(rx, ry, GRP_X, GRP_Y + 5 * ROW_H, GRP_W, ROW_H)) {
            wm_set_option(WM_OPT_REDUCE_TRANSP, !wm_get_option(WM_OPT_REDUCE_TRANSP));
            return 1;
        }
        if (in_rect(rx, ry, HIDE_X, GRP_Y + 3 * ROW_H, HIDE_W, ROW_H)) {      /* Dock hiding: Off / Auto / Smart */
            int k = (rx - HIDE_X) * 3 / HIDE_W;
            wm_set_option(WM_OPT_DOCK_HIDE, k < 0 ? 0 : (k > 2 ? 2 : k));
            return 1;
        }
        if (in_rect(rx, ry, GRP_X, GRP_Y + 4 * ROW_H, GRP_W, ROW_H)) {
            wm_set_option(WM_OPT_CLOCK24, !wm_get_option(WM_OPT_CLOCK24));
            return 1;
        }
        for (int i = 0; i < ACCENT_COUNT; i++) {
            i32 cx, cy;
            accent_pos(i, &cx, &cy);
            if ((rx - cx) * (rx - cx) + (ry - cy) * (ry - cy) <= 15 * 15) {
                theme_set_accent(i);
                return 1;
            }
        }
    }
    return 0;
}

/* ---------- окно About (как "Об этом Mac") ---------- */

#define AB_BTN_W 132
#define AB_BTN_H 34
#define AB_BTN_Y 392

void settings_show_about(void) { page = PAGE_ABOUT; }
void settings_show_page(int p) { page = p == 1 ? PAGE_THEMES : p == 2 ? PAGE_GLASS : PAGE_WALL; }

static void ab_row(i32 mid, i32 y, const char *label, const char *value, i32 maxw)
{
    const theme_t *t = theme();
    font_draw(mid - 8 - font_width(label, FONT_REGULAR), y, label, t->text, FONT_REGULAR);
    char buf[64];
    int n = 0;
    while (value[n] && n < 63) { buf[n] = value[n]; n++; }
    buf[n] = 0;
    while (n > 1 && font_width(buf, FONT_REGULAR) > maxw) {   /* длинное имя CPU обрезаем */
        n--;
        while (n > 1 && ((u8)buf[n] & 0xC0) == 0x80) n--;      /* не рвём UTF-8 */
        buf[n] = 0;
    }
    font_draw(mid, y, buf, t->sub, FONT_REGULAR);
}

static void ab_center(i32 cx, i32 y, const char *s, u32 col, int style)
{
    font_draw(cx - font_width(s, style) / 2, y, s, col, style);
}

void about_window_draw(i32 x, i32 y, i32 w, i32 h)
{
    (void)h;
    const theme_t *t = theme();
    i32 cx = x + w / 2;

    /* ноутбук EndixOS PC */
    i32 lx = cx - 64, ly = y + 26;
    fb_fill_round_rect(lx, ly, 128, 84, 7, RGB(28, 28, 32), 255);
    for (i32 j = 0; j < 76; j++) {
        u32 r = (u32)(58 + j * 40 / 76), g = (u32)(134 + j * 44 / 76), b = (u32)(214 + j * 26 / 76);
        fb_fill_rect(lx + 5, ly + 4 + j, 118, 1, RGB(r, g, b));
    }
    logo_draw(cx - 22, ly + 16, 44, RGB(255, 255, 255));
    fb_fill_round_rect(cx - 15, ly, 30, 6, 3, RGB(28, 28, 32), 255);
    fb_fill_round_rect(cx - 82, ly + 84, 164, 7, 3, RGB(206, 207, 212), 255);
    fb_fill_round_rect(cx - 22, ly + 84, 44, 3, 1, RGB(160, 161, 168), 255);

    ab_center(cx, y + 150, "EndixOS PC", t->text, FONT_TITLE);
    ab_center(cx, y + 190, "Version " ENDIX_VERSION ", 2026", t->sub, FONT_REGULAR);

    i32 mid = cx - 14, ry = y + 232, maxw = x + w - 10 - mid;
    char buf[40];
    char *p;
    ab_row(mid, ry, "Processor", si ? si->cpu : "?", maxw);
    p = put_u32(buf, si ? si->ram_mb : 0);
    put_str(p, " MB");
    ab_row(mid, ry + 26, "Memory", buf, maxw);
    ab_row(mid, ry + 52, "Startup disk", "EndixOS HD", maxw);
    p = put_u32(buf, si ? si->width : 0);
    p = put_str(p, " x ");
    put_u32(p, si ? si->height : 0);
    ab_row(mid, ry + 78, "Display", buf, maxw);
    ab_row(mid, ry + 104, "EndixOS", ENDIX_VERSION, maxw);

    i32 bx = cx - AB_BTN_W / 2, by = y + AB_BTN_Y;
    fb_fill_round_rect(bx, by, AB_BTN_W, AB_BTN_H, 9, theme_is_dark() ? RGB(80, 80, 88) : RGB(232, 232, 236), 255);
    ab_center(cx, by + (AB_BTN_H - font_height[FONT_REGULAR]) / 2, "More Info...", t->text, FONT_REGULAR);

    ab_center(cx, y + 446, "Copyright 2026 EndixOS", t->sub, FONT_REGULAR);
    ab_center(cx, y + 464, "All Rights Reserved.", t->sub, FONT_REGULAR);
}

int about_window_click(i32 rx, i32 ry, i32 w)
{
    i32 bx = w / 2 - AB_BTN_W / 2;
    if (rx >= bx && rx < bx + AB_BTN_W && ry >= AB_BTN_Y && ry < AB_BTN_Y + AB_BTN_H) {
        page = PAGE_ABOUT;
        return 1;
    }
    return 0;
}

/* Ползунок размера увеличения Dock: rx - координата мыши относительно содержимого окна. */
int settings_dragging(void) { return sl_drag; }

void settings_drag(i32 rx)
{
    i32 v = (rx - SL_X - 10) * 100 / (SL_W - 20);
    v = v < 0 ? 0 : (v > 100 ? 100 : v);
    if (sl_drag >= 2) fb_glass_param_set(sl_drag - 2, v);
    else wm_set_option(WM_OPT_DOCK_ZOOM, v);
}

void settings_release(void) { sl_drag = 0; }
