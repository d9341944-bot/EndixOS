/* Приложение Settings: боковое меню, страницы Wallpapers / Themes / About. */
#include "settings.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "wallpaper.h"

#define SIDEBAR_W 170
#define WIN_RADIUS 10
#define THUMB_W 130
#define THUMB_H 81
#define CARD_W 150
#define CARD_H 100

enum { PAGE_WALL, PAGE_THEMES, PAGE_ABOUT, PAGE_COUNT };

static const char *const page_name[PAGE_COUNT] = { "Wallpapers", "Themes", "About" };
static const u32 page_icon[PAGE_COUNT] = { RGB(52, 120, 246), RGB(255, 149, 0), RGB(110, 115, 125) };

static const sysinfo_t *si;
static int page = PAGE_WALL;
static int wall;
static u32 thumbs[WALL_COUNT][THUMB_W * THUMB_H];

/* ---------- геометрия (общая для рисования и кликов) ---------- */

static void thumb_pos(int i, i32 *x, i32 *y)
{
    *x = SIDEBAR_W + 24 + (i % 3) * (THUMB_W + 16);
    *y = 84 + (i / 3) * (THUMB_H + 38);
}

static void card_pos(int i, i32 *x, i32 *y)
{
    *x = SIDEBAR_W + 24 + i * (CARD_W + 20);
    *y = 84;
}

static void accent_pos(int i, i32 *cx, i32 *cy)
{
    *cx = SIDEBAR_W + 24 + 16 + i * 44;
    *cy = 266;
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

    fb_fill_round_rect(x + 28, y + 30, 72, 72, 18, theme_accent(), 255);
    i32 ew = font_width("E", FONT_TITLE);
    font_draw(x + 28 + (72 - ew) / 2, y + 30 + (72 - font_height[FONT_TITLE]) / 2 + 2,
              "E", RGB(255, 255, 255), FONT_TITLE);

    font_draw(x + 124, y + 26, "EndixOS", t->text, FONT_TITLE);
    font_draw(x + 125, y + 62, "Version 8.0 alpha 4", t->sub, FONT_REGULAR);

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
    heading(x, y, "Wallpapers", "Click a picture to set it as the desktop background");

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
    heading(x, y, "Themes", "Appearance and accent color");

    for (int i = 0; i < 2; i++) {
        i32 cx, cy;
        card_pos(i, &cx, &cy);
        draw_card(x + cx, y + cy, i, theme_is_dark() == i, i ? "Dark" : "Light");
    }

    font_draw(x + SIDEBAR_W + 24, y + 228, "Accent color", t->text, FONT_BOLD);
    font_draw(x + SIDEBAR_W + 24 + 110, y + 228, theme_accent_name(theme_accent_index()), t->sub, FONT_REGULAR);

    for (int i = 0; i < ACCENT_COUNT; i++) {
        i32 cx, cy;
        accent_pos(i, &cx, &cy);
        cx += x;
        cy += y;
        if (i == theme_accent_index()) {
            fb_fill_circle(cx, cy, 19, theme_accent_color(i));
            fb_fill_circle(cx, cy, 16, t->body);
        }
        fb_fill_circle(cx, cy, 14, theme_accent_color(i));
    }
}

/* ---------- окно целиком ---------- */

void settings_draw(i32 x, i32 y, i32 w, i32 h)
{
    const theme_t *t = theme();
    (void)w;

    /* боковая панель: левый нижний угол повторяет скругление окна */
    fb_fill_round_rect(x, y + h - 2 * WIN_RADIUS, SIDEBAR_W + WIN_RADIUS, 2 * WIN_RADIUS,
                       WIN_RADIUS, t->side, 255);
    fb_fill_rect(x + SIDEBAR_W, y + h - 2 * WIN_RADIUS, WIN_RADIUS, 2 * WIN_RADIUS, t->body);
    fb_fill_rect(x, y, SIDEBAR_W, h - WIN_RADIUS, t->side);
    fb_blend_rect(x + SIDEBAR_W - 1, y, 1, h, RGB(0, 0, 0), t->line_alpha);

    for (int i = 0; i < PAGE_COUNT; i++) {
        i32 ix = x + 10, iy = y + 14 + i * 34;
        int sel = (i == page);
        if (sel)
            fb_fill_round_rect(ix, iy, SIDEBAR_W - 20, 28, 7, theme_accent(), 255);
        if (sel)                                    /* белая обводка, чтобы иконка не сливалась с подсветкой */
            fb_fill_round_rect(ix + 7, iy + 4, 20, 20, 6, RGB(255, 255, 255), 255);
        fb_fill_round_rect(ix + 8, iy + 5, 18, 18, 5, i == PAGE_ABOUT ? theme_accent() : page_icon[i], 255);
        font_draw(ix + 36, iy + 5, page_name[i], sel ? RGB(255, 255, 255) : t->text, FONT_REGULAR);
    }

    switch (page) {
    case PAGE_WALL:   draw_wallpapers(x, y); break;
    case PAGE_THEMES: draw_themes(x, y);     break;
    case PAGE_ABOUT:  about_draw(x + SIDEBAR_W, y); break;
    }
}

int settings_click(i32 rx, i32 ry)
{
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (in_rect(rx, ry, 10, 14 + i * 34, SIDEBAR_W - 20, 28)) {
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
    } else if (page == PAGE_THEMES) {
        for (int i = 0; i < 2; i++) {
            i32 cx, cy;
            card_pos(i, &cx, &cy);
            if (in_rect(rx, ry, cx, cy, CARD_W, CARD_H)) {
                theme_set_dark(i);
                return 1;
            }
        }
        for (int i = 0; i < ACCENT_COUNT; i++) {
            i32 cx, cy;
            accent_pos(i, &cx, &cy);
            if ((rx - cx) * (rx - cx) + (ry - cy) * (ry - cy) <= 18 * 18) {
                theme_set_accent(i);
                return 1;
            }
        }
    }
    return 0;
}
