/* Приложение Settings: боковое меню, страницы Wallpapers / Themes / About. */
#include "settings.h"
#include "../drivers/ps2.h"
#include "wm.h"
#include "icons.h"
#include "avatar.h"
#include "login.h"
#include "fb.h"
#include "font.h"
#include "lang.h"
#include "theme.h"
#include "logo.h"
#include "ui.h"
#include "wallpaper.h"
#include "photos.h"
#include "scenery.h"
#include "timeset.h"
#include "resolution.h"
#include "update.h"
#include "wifi.h"
#include "perf.h"
#include "control.h"
#include "../kernel/event.h"

#define SIDEBAR_W 170
#define SIDE_ITEMS_Y 94
#define SIDE_PITCH 27
#define WIN_RADIUS 14
#define THUMB_W 100
#define THUMB_H 62
#define CARD_W 150
#define CARD_H 100

enum { PAGE_WALL, PAGE_PHOTOS, PAGE_THEMES, PAGE_GLASS, PAGE_ICONS, PAGE_DISPLAY, PAGE_SOUND, PAGE_TRACKPAD, PAGE_WIFI, PAGE_DATETIME, PAGE_LANG, PAGE_UPDATE, PAGE_ACCOUNT, PAGE_ABOUT, PAGE_COUNT };

static const char *const page_name[PAGE_COUNT] = { "Wallpaper", "Photos", "Appearance", "Liquid Glass", "Icons", "Displays", "Sound", "Trackpad", "Wi-Fi", "Date & Time", "Language & Region", "Software Update", "Account", "About" };
static const char *const page_keys[PAGE_COUNT] = {
    "wallpaper background picture desktop", "photos pictures jpeg png own images wallpaper", "appearance dark light accent color dock clock 24 hour transparency magnification",
    "liquid glass blur refraction frost", "icons style glass mono tinted color one color app icons", "displays brightness screen display", "sound volume audio", "trackpad touchpad mouse tap click scroll cursor speed natural pointer", "wifi wi-fi wireless network internet connection router", "date time clock calendar time zone timezone hour minute year month day sync automatic ntp internet", "language russian english русский английский region keyboard layout раскладка язык", "software update upgrade version check install restart download", "account user avatar picture emoji photo lock screen login", "about system info version memory" };

/* поле Search в боковой панели */
static char sq[24]; static int sqn, sq_focus;
static int page_match(int i)
{
    if (!sqn) return 1;
    const char *hay[3] = { page_name[i], page_keys[i], tr(page_name[i]) };
    for (int h = 0; h < 3; h++)
        for (const char *p = hay[h]; *p; p++) {
            int k = 0;
            while (k < sqn && p[k] && (p[k] | 32) == (sq[k] | 32)) k++;
            if (k == sqn) return 1;
        }
    return 0;
}
static int vis_pages(int *out)                          /* страницы, подходящие под запрос, по порядку */
{
    int n = 0;
    for (int i = 0; i < PAGE_COUNT; i++) if (page_match(i)) out[n++] = i;
    return n;
}

static const sysinfo_t *si;
static int page = PAGE_WALL;
static int wall;
static u32 thumbs[WALL_RENDERED][THUMB_W * THUMB_H];
static u8  thumb_ok[WALL_RENDERED], thumb_ph[WALL_RENDERED];
static u32 pthumbs[PHOTO_MAX][THUMB_W * THUMB_H];
static u8  pthumb_ok[PHOTO_MAX];            /* 0 - не пробовали, 1 - готова, 2 - не читается */

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
    int okid = id >= 0 && (id < WALL_COUNT || (id < WALL_PHOTO0 && id - WALL_COUNT < scenery_count()) ||
                           (id >= WALL_PHOTO0 && id - WALL_PHOTO0 < photos_count()));
    if (!okid)
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
    settings_set_wallpaper(scenery_count() > 0 ? WALL_COUNT : 0);
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

static void about_laptop(i32 cx, i32 ly)                /* ноутбук EndixOS PC */
{
    i32 lx = cx - 64;
    fb_fill_round_rect(lx, ly, 128, 84, 7, RGB(28, 28, 32), 255);
    for (i32 j = 0; j < 76; j++) {
        u32 r = (u32)(58 + j * 40 / 76), g = (u32)(134 + j * 44 / 76), b = (u32)(214 + j * 26 / 76);
        fb_fill_rect(lx + 5, ly + 4 + j, 118, 1, RGB(r, g, b));
    }
    logo_draw(cx - 22, ly + 16, 44, RGB(255, 255, 255));
    fb_fill_round_rect(cx - 15, ly, 30, 6, 3, RGB(28, 28, 32), 255);
    fb_fill_round_rect(cx - 82, ly + 84, 164, 7, 3, RGB(206, 207, 212), 255);
    fb_fill_round_rect(cx - 22, ly + 84, 44, 3, 1, RGB(160, 161, 168), 255);
}

#define AB_G_Y 206
#define AB_ROW 34
#define AB_ROWS 5
#define AB_UPD_Y (AB_G_Y + AB_ROWS * AB_ROW + 16)
#define AB_UPD_W 170

static void ab_cent(i32 cx, i32 y, const char *s, u32 c, int font) { font_draw(cx - font_width(s, font) / 2, y, s, c, font); }

void about_draw(i32 x, i32 y)
{
    const theme_t *t = theme();
    char buf[96];
    i32 gx = x + 24, cx = gx + GRP_W / 2;

    about_laptop(cx, y + 26);
    ab_cent(cx, y + 128, "EndixOS PC", t->text, FONT_TITLE);
    char *p = put_str(buf, "Version " ENDIX_VERSION "  (build ");
    p = put_u32(p, ENDIX_BUILD);
    put_str(p, ")");
    ab_cent(cx, y + 170, buf, t->sub, FONT_REGULAR);

    ui_group(gx, y + AB_G_Y, GRP_W, AB_ROW * AB_ROWS);
    const char *adapter = wifi_adapter();
    char mem[24], disp[24];
    p = put_u32(mem, si ? si->ram_mb : 0); put_str(p, " MB");
    p = put_u32(disp, si ? si->width : 0); p = put_str(p, " x "); put_u32(p, si ? si->height : 0);
    const char *lab[AB_ROWS] = { "Processor", "Memory", "Display", "Wi-Fi", "Kernel" };
    const char *val[AB_ROWS] = { si ? si->cpu : "?", mem, disp, adapter[0] ? adapter : "not found", "EndixOS i686, from scratch" };
    for (int i = 0; i < AB_ROWS; i++) {
        i32 ry = y + AB_G_Y + i * AB_ROW;
        if (i) ui_hline(gx + 16, ry, GRP_W - 16);
        font_draw(gx + 16, ry + 9, lab[i], t->sub, FONT_REGULAR);
        i32 vw = font_width(val[i], FONT_REGULAR);
        font_draw(gx + GRP_W - 16 - (vw > GRP_W - 130 ? GRP_W - 130 : vw), ry + 9, val[i], t->text, FONT_REGULAR);
    }
    ui_button(gx, y + AB_UPD_Y, AB_UPD_W, 30, "Software Update...", 1, 0);
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
    heading(x, y, "Wallpaper", "3D scenes - click one to set it as the desktop");

    int n = scenery_count(), loaded = 0;              /* одна миниатюра за кадр */
    if (n > WALL_RENDERED) n = WALL_RENDERED;
    for (int i = 0, slot = 0; i < n; i++) {
        if (scenery_hidden(i)) continue;
        i32 tx, ty;
        thumb_pos(slot++, &tx, &ty);
        tx += x;
        ty += y;
        if (thumb_ok[i] == 1 && thumb_ph[i] != scenery_night()) thumb_ok[i] = 0;       /* сменились день/ночь */
        if (!thumb_ok[i] && !loaded) {
            loaded = 1;
            thumb_ok[i] = scenery_paint(i, thumbs[i], THUMB_W, THUMB_H) ? 1 : 2;
            thumb_ph[i] = (u8)scenery_night();
        }
        int sel = wall == WALL_COUNT + i;
        if (sel)
            fb_fill_round_rect(tx - 3, ty - 3, THUMB_W + 6, THUMB_H + 6, 7, theme_accent(), 255);
        else
            fb_fill_round_rect(tx - 1, ty - 1, THUMB_W + 2, THUMB_H + 2, 5, RGB(0, 0, 0), 40);
        if (thumb_ok[i] == 1) fb_blit(tx, ty, thumbs[i], THUMB_W, THUMB_H);
        else fb_fill_round_rect(tx, ty, THUMB_W, THUMB_H, 6, t->card, 255);
        const char *name = scenery_label(i);
        font_draw(tx + (THUMB_W - font_width(name, FONT_REGULAR)) / 2, ty + THUMB_H + 7,
                  name, sel ? t->text : t->sub, FONT_REGULAR);
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

static void draw_photos(i32 x, i32 y)
{
    const theme_t *t = theme();
    heading(x, y, "Photos", "Your pictures as wallpaper");
    int n = photos_count();
    if (n == 0) {
        static const char *const hint[4] = {
            "No pictures found.",
            "Put JPEG or PNG files into the folder \"Pictures\" on the",
            "system disk (the FAT32 partition), then restart EndixOS.",
            "JPEG must be baseline (not progressive), up to 4096 px wide." };
        for (int i = 0; i < 4; i++)
            font_draw(x + SIDEBAR_W + 25, y + 96 + i * 22, hint[i], i ? t->sub : t->text, FONT_REGULAR);
        return;
    }
    int loaded = 0;                                   /* одна миниатюра за кадр: декодирование не подвешивает интерфейс */
    for (int i = 0; i < n; i++) {
        i32 tx, ty;
        thumb_pos(i, &tx, &ty);
        tx += x; ty += y;
        if (!pthumb_ok[i] && !loaded) {
            loaded = 1;
            pthumb_ok[i] = photos_paint(i, pthumbs[i], THUMB_W, THUMB_H) ? 1 : 2;
        }
        if (wall == WALL_PHOTO0 + i) fb_fill_round_rect(tx - 3, ty - 3, THUMB_W + 6, THUMB_H + 6, 8, theme_accent(), 255);
        if (pthumb_ok[i] == 1)
            for (i32 r = 0; r < THUMB_H; r++)
                for (i32 c = 0; c < THUMB_W; c++) fb_fill_rect(tx + c, ty + r, 1, 1, pthumbs[i][r * THUMB_W + c]);
        else {
            fb_fill_round_rect(tx, ty, THUMB_W, THUMB_H, 6, t->card, 255);
            font_draw(tx + 10, ty + 22, pthumb_ok[i] == 2 ? "Can't open" : "Loading...", t->sub, FONT_REGULAR);
        }
        char nm[24]; int k = 0;
        for (; photos_name(i)[k] && k < 14; k++) nm[k] = photos_name(i)[k];
        nm[k] = 0;
        font_draw(tx + (THUMB_W - font_width(nm, FONT_REGULAR)) / 2, ty + THUMB_H + 6, nm, t->text, FONT_REGULAR);
    }
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


/* ---------- Значки приложений: стиль ---------- */
#define IC_PV_Y 84
#define IC_PV_H 96
#define IC_SEG_W 300
#define IC_SEG_X (GRP_X + GRP_W - 16 - IC_SEG_W)
#define IC_ROW_Y (IC_PV_Y + IC_PV_H + 20)

static void draw_icons(i32 x, i32 y)
{
    const theme_t *t = theme();
    heading(x, y, "Icons", "Style of the app icons in the Dock and Launchpad");
    i32 px = x + GRP_X, py = y + IC_PV_Y;
    for (i32 i = 0; i < GRP_W; i++) {                                    /* пёстрый фон: сквозь стекло видно, что оно прозрачное */
        u32 r = (u32)(60 + i * 150 / GRP_W), g = (u32)(70 + (GRP_W - i) * 80 / GRP_W), b = (u32)(200 - i * 80 / GRP_W);
        fb_fill_rect(px + i, py, 1, IC_PV_H, RGB(r, g, b));
    }
    fb_fill_circle(px + 60, py + 30, 30, RGB(255, 214, 80));
    fb_fill_circle(px + 360, py + 70, 34, RGB(60, 224, 230));
    static const int ids[7] = { 0, 5, 1, 2, 3, 4, ICON_LAUNCHPAD };
    for (int k = 0; k < 7; k++) icon_draw(ids[k], px + 14 + k * 58, py + 24, 48);
    ui_group(x + GRP_X, y + IC_ROW_Y, GRP_W, ROW_H);
    font_draw(x + GRP_X + 16, y + IC_ROW_Y + 13, "Icon style", t->text, FONT_REGULAR);
    static const char *const labels[ICON_STYLE_COUNT] = { "Default", "Glass", "Mono", "Tinted" };
    ui_segment(x + IC_SEG_X, y + IC_ROW_Y + 9, IC_SEG_W, labels, ICON_STYLE_COUNT, icon_style());
    static const char *const hint[ICON_STYLE_COUNT] = {
        "Colorful icons, as always.",
        "Translucent glass tiles with a bright edge.",
        "One color: dark glass tiles with light symbols.",
        "One color: tiles and symbols use the accent color (Appearance)." };
    font_draw(x + GRP_X + 2, y + IC_ROW_Y + ROW_H + 8, hint[icon_style()], t->sub, FONT_REGULAR);
}


/* ---------- Учётная запись: аватарка ---------- */
#define AC_PV_Y 84
#define AC_PV_H 96
#define AC_CELL 46
#define AC_GAP 4
#define AC_COLS 8
#define AC_GRID_Y (AC_PV_Y + AC_PV_H + 14)
#define AC_GRID_H (2 * (AC_CELL + AC_GAP) + 12)
#define AC_COL_Y (AC_GRID_Y + AC_GRID_H + 14)
#define AC_COL_H 50

static void draw_account(i32 x, i32 y)
{
    const theme_t *t = theme();
    heading(x, y, "Account", "Your picture on the lock screen and in Settings");
    ui_group(x + GRP_X, y + AC_PV_Y, GRP_W, AC_PV_H);
    avatar_draw(x + GRP_X + 16 + 36, y + AC_PV_Y + AC_PV_H / 2, 36);
    const char *u = login_user();
    font_draw(x + GRP_X + 108, y + AC_PV_Y + 28, u && u[0] ? u : "endix", t->text, FONT_TITLE);
    font_draw(x + GRP_X + 109, y + AC_PV_Y + 62, "EndixOS user", t->sub, FONT_REGULAR);

    ui_group(x + GRP_X, y + AC_GRID_Y, GRP_W, AC_GRID_H);
    i32 gx0 = x + GRP_X + (GRP_W - (AC_COLS * (AC_CELL + AC_GAP) - AC_GAP)) / 2;
    for (int k = 0; k <= AVATAR_EMOJI; k++) {
        i32 cx = gx0 + (k % AC_COLS) * (AC_CELL + AC_GAP), cy = y + AC_GRID_Y + 10 + (k / AC_COLS) * (AC_CELL + AC_GAP);
        if (k == avatar_get()) fb_fill_round_rect(cx - 2, cy - 2, AC_CELL + 4, AC_CELL + 4, 12, theme_accent(), 255);
        else fb_fill_round_rect(cx, cy, AC_CELL, AC_CELL, 10, t->side, 255);
        avatar_draw_as(k, k == 0 ? 0 : avatar_color(), cx + AC_CELL / 2, cy + AC_CELL / 2, AC_CELL / 2 - 4);
    }
    ui_group(x + GRP_X, y + AC_COL_Y, GRP_W, AC_COL_H);
    font_draw(x + GRP_X + 16, y + AC_COL_Y + 17, "Background", t->text, FONT_REGULAR);
    for (int c = 0; c < AVATAR_COLORS; c++) {
        i32 cx = x + GRP_X + GRP_W - 24 - (AVATAR_COLORS - 1 - c) * 28, cy = y + AC_COL_Y + AC_COL_H / 2;
        if (c == avatar_color()) fb_fill_circle(cx, cy, 12, t->text);
        fb_fill_circle(cx, cy, c == avatar_color() ? 10 : 11, avatar_color_rgb(c));
    }
    font_draw(x + GRP_X + 2, y + AC_COL_Y + AC_COL_H + 10, "The first picture is the default silhouette.", t->sub, FONT_REGULAR);
}

/* ---------- Liquid Glass ---------- */

static const char *const glass_label[GLASS_PARAMS] = { "Refraction", "Blur", "Frost", "Edge light", "Vibrancy" };
#define PV_Y 84
#define PV_H 116
#define RST_Y (GRP_Y + GLASS_PARAMS * ROW_H + 12)

#define PERF_Y 136
#define PERF_W 250
#define PERF_X (GRP_X + GRP_W - 16 - PERF_W)
#define RES_Y 252
#define RES_ROW 26
static int res_sel = -1;
static const char *res_msg = "";

static void draw_display(i32 x, i32 y)
{
    const theme_t *t = theme();
    heading(x, y, "Displays", "Brightness and screen resolution");
    ui_group(x + GRP_X, y + 84, GRP_W, ROW_H);
    font_draw(x + GRP_X + 16, y + 84 + 13, "Brightness", t->text, FONT_REGULAR);
    ui_slider(x + SL_X, y + 84 + ROW_H / 2, SL_W, (cc_brightness() - 20) * 100 / 80, 1);
    ui_group(x + GRP_X, y + PERF_Y, GRP_W, ROW_H);
    font_draw(x + GRP_X + 16, y + PERF_Y + 13, "Performance", t->text, FONT_REGULAR);
    static const char *const perf_labels[PERF_MODES] = { "Auto", "High", "Balanced", "Fast" };
    ui_segment(x + PERF_X, y + PERF_Y + 9, PERF_W, perf_labels, PERF_MODES, perf_mode());
    {
        static const char *const lv[3] = { "Full quality", "Lighter glass, no shadows", "No glass blur, no shadows" };
        font_draw(x + GRP_X + 2, y + PERF_Y + ROW_H + 8, lv[perf_level()], t->sub, FONT_REGULAR);
    }
    font_draw(x + GRP_X + 2, y + RES_Y - 22, "Resolution (applies after restart)", t->sub, FONT_BOLD);
    ui_group(x + GRP_X, y + RES_Y, GRP_W, res_count() * RES_ROW);
    int cur = res_current();
    for (int i = 0; i < res_count(); i++) {
        i32 ry = y + RES_Y + i * RES_ROW;
        if (i == res_sel) fb_fill_round_rect(x + GRP_X + 6, ry + 3, GRP_W - 12, RES_ROW - 6, 6, theme_accent(), 70);
        font_draw(x + GRP_X + 16, ry + 8, res_name(i), t->text, FONT_REGULAR);
        if (i == cur) font_draw(x + GRP_X + GRP_W - 16 - font_width("current", FONT_REGULAR), ry + 8, "current", t->sub, FONT_REGULAR);
    }
    if (res_msg[0]) font_draw(x + GRP_X + 2, y + RES_Y + res_count() * RES_ROW + 10, res_msg, t->sub, FONT_REGULAR);
    else if (!res_available()) font_draw(x + GRP_X + 2, y + RES_Y + res_count() * RES_ROW + 10, "Saving is available on an installed system.", t->sub, FONT_REGULAR);
}

/* ---------- Wi-Fi ---------- */
#define WF_LIST_Y (84 + ROW_H + 76)
static int wf_sel = -1, wf_focus, wf_pwn; static char wf_pw[64];
static void draw_lock(i32 x, i32 y, u32 c)
{
    fb_fill_round_rect(x, y + 5, 11, 8, 2, c, 255);
    fb_fill_round_rect(x + 2, y, 7, 3, 1, c, 255); fb_fill_round_rect(x + 3, y + 2, 5, 3, 1, c, 255);
}
static const char *fit_text(const char *s, int maxw, int font)
{
    static char buf[4][72]; static int bi;
    char *o = buf[bi++ & 3]; int n = 0;
    while (s[n] && n < 60) { o[n] = s[n]; n++; }
    o[n] = 0;
    if (font_width(o, font) <= maxw) return o;
    while (n > 1) { n--; o[n] = '.'; o[n+1] = '.'; o[n+2] = '.'; o[n+3] = 0; if (font_width(o, font) <= maxw) return o; }
    return o;
}

static void draw_wifi(i32 x, i32 y)
{
    const theme_t *t = theme();
    wifi_refresh();
    heading(x, y, "Wi-Fi", "Connect to a wireless network");
    int s = wifi_state();
    ui_group(x + GRP_X, y + 84, GRP_W, ROW_H);
    font_draw(x + GRP_X + 16, y + 84 + 13, "Wi-Fi", t->text, FONT_REGULAR);
    ui_switch(x + GRP_X + GRP_W - 56, y + 84 + 10, s == WIFI_ON || s == WIFI_STARTING);
    font_draw(x + GRP_X + 2, y + 84 + ROW_H + 10, wifi_adapter()[0] ? wifi_adapter() : "No adapter", t->sub, FONT_BOLD);
    font_draw(x + GRP_X + 2, y + 84 + ROW_H + 28, fit_text(wifi_message(), GRP_W - 4, FONT_REGULAR), t->sub, FONT_REGULAR);
    if (s == WIFI_STARTING || wifi_busy()) {
        static int spin;
        spin++;
        for (int k = 0; k < 8; k++) {
            static const i8 sx[8] = { 0, 7, 10, 7, 0, -7, -10, -7 }, sy[8] = { -10, -7, 0, 7, 10, 7, 0, -7 };
            int a = 40 + ((8 + k - (spin / 2) % 8) % 8) * 26;
            fb_fill_circle(x + GRP_X + GRP_W - 80 + sx[k], y + 84 + ROW_H / 2 + sy[k] * 6 / 10, 2, RGB(a, a, a + 6 > 255 ? 255 : a + 6));
        }
    }
    if (s == WIFI_ON) {
        font_draw(x + GRP_X + 2, y + WF_LIST_Y - 20, "Networks", t->sub, FONT_BOLD);
        if (wf_sel < 0) ui_button(x + GRP_X + GRP_W - 70, y + WF_LIST_Y - 26, 70, 24, "Scan", !wifi_busy(), 0);
        if (wf_sel >= 0) {                                           /* ввод пароля */
            ui_group(x + GRP_X, y + WF_LIST_Y, GRP_W, ROW_H * 2 + 8);
            font_draw(x + GRP_X + 16, y + WF_LIST_Y + 12, "Password for", t->sub, FONT_REGULAR);
            font_draw(x + GRP_X + 112, y + WF_LIST_Y + 12, fit_text(wifi_net_name(wf_sel), GRP_W - 130, FONT_BOLD), t->text, FONT_BOLD);
            char dots[66]; int i = 0; for (; i < wf_pwn && i < 40; i++) dots[i] = '*'; dots[i] = 0;
            fb_fill_round_rect(x + GRP_X + 16, y + WF_LIST_Y + 36, GRP_W - 32, 28, 6, t->body, 255);
            font_draw(x + GRP_X + 24, y + WF_LIST_Y + 43, dots, t->text, FONT_REGULAR);
            if (wf_focus) fb_fill_rect(x + GRP_X + 26 + font_width(dots, FONT_REGULAR), y + WF_LIST_Y + 41, 1, 16, t->text);
            if (wf_pwn == 0) font_draw(x + GRP_X + 24, y + WF_LIST_Y + 43, "Enter password", t->sub, FONT_REGULAR);
            ui_button(x + GRP_X + GRP_W - 180, y + WF_LIST_Y + ROW_H * 2 + 20, 80, 28, "Cancel", 1, 0);
            ui_button(x + GRP_X + GRP_W - 90, y + WF_LIST_Y + ROW_H * 2 + 20, 90, 28, "Join", wf_pwn >= 8, 1);
        } else {
            int n = wifi_net_count(); if (n > WIFI_SHOW) n = WIFI_SHOW;
            ui_group(x + GRP_X, y + WF_LIST_Y, GRP_W, (n ? n : 1) * ROW_H);
            if (!n) font_draw(x + GRP_X + 16, y + WF_LIST_Y + 13, wifi_busy() ? "Searching..." : "No networks found", t->sub, FONT_REGULAR);
            for (int k = 0; k < n; k++) {
                i32 ry = y + WF_LIST_Y + k * ROW_H;
                if (k) ui_hline(x + GRP_X + 16, ry, GRP_W - 32);
                int c = wifi_net_connected(k), sec = wifi_net_secure(k);
                font_draw(x + GRP_X + 16, ry + 13, fit_text(wifi_net_name(k), (c || sec == 2) ? GRP_W - 180 : GRP_W - 90, c ? FONT_BOLD : FONT_REGULAR), sec == 2 ? t->sub : t->text, c ? FONT_BOLD : FONT_REGULAR);
                if (c) font_draw(x + GRP_X + GRP_W - 150, ry + 13, "Connected", t->sub, FONT_REGULAR);
                else if (sec == 2) font_draw(x + GRP_X + GRP_W - 150, ry + 13, "Not supported", t->sub, FONT_REGULAR);
                if (sec == 1) draw_lock(x + GRP_X + GRP_W - 62, ry + 14, t->text);
                for (int b = 0; b < 4; b++)
                    fb_fill_round_rect(x + GRP_X + GRP_W - 40 + b * 7, ry + 26 - b * 4, 4, 5 + b * 4, 1, b < wifi_net_signal(k) ? t->text : t->sub, b < wifi_net_signal(k) ? 255 : 90);
            }
        }
    }
    wifi_step();
}

static int wifi_page_click(i32 rx, i32 ry)
{
    int s = wifi_state();
    if (in_rect(rx, ry, GRP_X, 84, GRP_W, ROW_H)) { wifi_refresh(); wf_sel = -1; wf_focus = 0; wifi_set_enabled(s != WIFI_ON && s != WIFI_STARTING); return 1; }
    if (s != WIFI_ON) return 0;
    if (wf_sel >= 0) {
        if (in_rect(rx, ry, GRP_X + GRP_W - 90, WF_LIST_Y + ROW_H * 2 + 20, 90, 28)) { if (wf_pwn >= 8 && wifi_connect(wf_sel, wf_pw) == 0) { wf_sel = -1; wf_focus = 0; } return 1; }
        if (in_rect(rx, ry, GRP_X + GRP_W - 180, WF_LIST_Y + ROW_H * 2 + 20, 80, 28)) { wf_sel = -1; wf_focus = 0; return 1; }
        wf_focus = in_rect(rx, ry, GRP_X + 16, WF_LIST_Y + 36, GRP_W - 32, 28);
        return 1;
    }
    if (in_rect(rx, ry, GRP_X + GRP_W - 70, WF_LIST_Y - 26, 70, 24)) { wifi_scan(); return 1; }
    int n = wifi_net_count(); if (n > WIFI_SHOW) n = WIFI_SHOW;
    for (int k = 0; k < n; k++)
        if (in_rect(rx, ry, GRP_X, WF_LIST_Y + k * ROW_H, GRP_W, ROW_H)) {
            int sec = wifi_net_secure(k);
            if (wifi_net_connected(k)) wifi_disconnect();
            else if (sec == 0) wifi_connect(k, "");
            else if (sec == 1 && !wifi_busy()) { wf_sel = k; wf_pwn = 0; wf_pw[0] = 0; wf_focus = 1; }
            return 1;
        }
    return 0;
}
static int wifi_page_key(i32 key)
{
    if (wf_sel < 0 || !wf_focus) return 0;
    if (key == 27) { wf_sel = -1; wf_focus = 0; }
    else if (key == '\b') { if (wf_pwn) wf_pw[--wf_pwn] = 0; }
    else if (key == '\n') { if (wf_pwn >= 8 && wifi_connect(wf_sel, wf_pw) == 0) { wf_sel = -1; wf_focus = 0; } }
    else if (key >= 32 && key < 127 && wf_pwn < 63) { wf_pw[wf_pwn++] = (char)key; wf_pw[wf_pwn] = 0; }
    else return 0;
    return 1;
}

static void draw_sound(i32 x, i32 y)
{
    const theme_t *t = theme();
    heading(x, y, "Sound", "Output volume");
    ui_group(x + GRP_X, y + 84, GRP_W, ROW_H);
    font_draw(x + GRP_X + 16, y + 84 + 13, "Output volume", t->text, FONT_REGULAR);
    ui_slider(x + SL_X, y + 84 + ROW_H / 2, SL_W, cc_volume(), 1);
}

__attribute__((weak)) int ps2_tp_present(void) { return 0; }
__attribute__((weak)) void ps2_tp_get(int *a, int *b, int *c) { *a = 50; *b = 1; *c = 1; }
__attribute__((weak)) void ps2_tp_set(int a, int b, int c) { (void)a; (void)b; (void)c; }

static void draw_trackpad(i32 x, i32 y)
{
    const theme_t *t = theme();
    int sp, tap, nat;
    ps2_tp_get(&sp, &tap, &nat);
    heading(x, y, "Trackpad", ps2_tp_present() ? "Synaptics touchpad detected" : "No touchpad detected: using the standard mouse");
    ui_group(x + GRP_X, y + 84, GRP_W, ROW_H * 3);
    font_draw(x + GRP_X + 16, y + 84 + 13, "Tracking speed", t->text, FONT_REGULAR);
    ui_slider(x + SL_X, y + 84 + ROW_H / 2, SL_W, sp, 1);
    ui_hline(x + GRP_X + 16, y + 84 + ROW_H, GRP_W - 32);
    font_draw(x + GRP_X + 16, y + 84 + ROW_H + 13, "Tap to click", t->text, FONT_REGULAR);
    ui_switch(x + GRP_X + GRP_W - 52, y + 84 + ROW_H + 9, tap);
    ui_hline(x + GRP_X + 16, y + 84 + 2 * ROW_H, GRP_W - 32);
    font_draw(x + GRP_X + 16, y + 84 + 2 * ROW_H + 13, "Natural scrolling", t->text, FONT_REGULAR);
    ui_switch(x + GRP_X + GRP_W - 52, y + 84 + 2 * ROW_H + 9, nat);
    font_draw(x + GRP_X, y + 84 + 3 * ROW_H + 14, "Tap with two fingers for a right click. Scroll with two fingers.", t->sub, FONT_REGULAR);
}

/* ---------- Date & Time ---------- */
#define DT_A_Y 84
#define DT_S_Y (DT_A_Y + 3 * ROW_H)
#define DT_B_Y (DT_S_Y + 66)
static u32 dr[5];                                       /* черновик: день, месяц, год, час, минуты */
static int dr_ok;
static const char *const mon_name[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static char *put_num(char *p, u32 v, int w)
{
    char t[10]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = n; i < w; i++) *p++ = '0';
    while (n) *p++ = t[--n];
    return p;
}
static void dt_draft(void)
{
    if (dr_ok) return;
    u32 h, m, d, mo, y;
    ts_now(&h, &m, &d, &mo, &y);
    dr[0] = d; dr[1] = mo; dr[2] = y; dr[3] = h; dr[4] = m;
    dr_ok = 1;
}
static void dt_step(int k, int dir)
{
    dt_draft();
    static const int lo[5] = { 1, 1, 2020, 0, 0 }, hi[5] = { 31, 12, 2099, 23, 59 };
    int v = (int)dr[k] + dir, mx = hi[k];
    if (k == 0) mx = ts_days_in_month(dr[2], dr[1]);
    if (v > mx) v = (k == 2) ? mx : lo[k];
    if (v < lo[k]) v = (k == 2) ? lo[k] : mx;
    dr[k] = (u32)v;
    int dm = ts_days_in_month(dr[2], dr[1]);
    if ((int)dr[0] > dm) dr[0] = (u32)dm;
}
static void dt_stepper_pos(int k, i32 *cx, i32 *cy) { *cx = GRP_X + 10 + (k % 3) * 138; *cy = DT_B_Y + (k < 3 ? 0 : ROW_H) + 9; }
#define DT_TZ_PX (GRP_X + GRP_W - 16 - 26)

static void draw_datetime(i32 x, i32 y)
{
    const theme_t *t = theme();
    ts_poll();
    dt_draft();
    heading(x, y, tr("Date & Time"), tr("Set the clock, the time zone and automatic sync"));
    u32 h, m, d, mo, yr;
    ts_now(&h, &m, &d, &mo, &yr);
    char b[40], *p = b;
    p = put_num(p, d, 2); *p++ = ' '; for (const char *q = mon_name[(mo + 11) % 12]; *q; q++) *p++ = *q;
    *p++ = ' '; p = put_num(p, yr, 4); *p++ = ','; *p++ = ' '; p = put_num(p, h, 2); *p++ = ':'; p = put_num(p, m, 2); *p = 0;
    ui_group(x + GRP_X, y + DT_A_Y, GRP_W, ROW_H * 3);
    font_draw(x + GRP_X + 16, y + DT_A_Y + 13, tr("Current time"), t->text, FONT_REGULAR);
    font_draw(x + GRP_X + GRP_W - 16 - font_width(b, FONT_REGULAR), y + DT_A_Y + 13, b, t->sub, FONT_REGULAR);
    ui_hline(x + GRP_X + 16, y + DT_A_Y + ROW_H, GRP_W - 32);
    font_draw(x + GRP_X + 16, y + DT_A_Y + ROW_H + 13, tr("Time zone"), t->text, FONT_REGULAR);
    int tz = ts_tz(), at = tz < 0 ? -tz : tz;
    p = b; for (const char *q = "UTC"; *q; q++) *p++ = *q;
    *p++ = tz < 0 ? '-' : '+'; p = put_num(p, (u32)at / 60, 2); *p++ = ':'; p = put_num(p, (u32)at % 60, 2); *p = 0;
    i32 ty = y + DT_A_Y + ROW_H + 9;
    ui_button(x + DT_TZ_PX - 96 - 26, ty, 26, 24, "-", 1, 0);
    font_draw(x + DT_TZ_PX - 96 + (96 - font_width(b, FONT_REGULAR)) / 2, ty + 4, b, t->text, FONT_REGULAR);
    ui_button(x + DT_TZ_PX, ty, 26, 24, "+", 1, 0);
    ui_hline(x + GRP_X + 16, y + DT_A_Y + 2 * ROW_H, GRP_W - 32);
    font_draw(x + GRP_X + 16, y + DT_A_Y + 2 * ROW_H + 13, tr("Set automatically"), t->text, FONT_REGULAR);
    ui_switch(x + GRP_X + GRP_W - 52, y + DT_A_Y + 2 * ROW_H + 9, ts_auto());
    font_draw(x + GRP_X, y + DT_S_Y + 14, ts_msg()[0] ? ts_msg() : "Time comes from the internet (needs a connection).", t->sub, FONT_REGULAR);
    ui_button(x + GRP_X + GRP_W - 96, y + DT_S_Y + 6, 96, 28, ts_state() == TS_BUSY ? tr("Syncing...") : tr("Sync now"), ts_state() != TS_BUSY, 0);
    font_draw(x + GRP_X, y + DT_B_Y - 20, tr("Set manually"), t->sub, FONT_REGULAR);
    ui_group(x + GRP_X, y + DT_B_Y, GRP_W, ROW_H * 2);
    ui_hline(x + GRP_X + 16, y + DT_B_Y + ROW_H, GRP_W - 32);
    const char *const lab[5] = { tr("Day"), tr("Month"), tr("Year"), tr("Hour"), tr("Min") };
    for (int k = 0; k < 5; k++) {
        i32 cx, cy;
        dt_stepper_pos(k, &cx, &cy);
        cx += x; cy += y;
        font_draw(cx, cy + 4, lab[k], t->sub, FONT_REGULAR);
        ui_button(cx + 44, cy, 22, 24, "-", 1, 0);
        if (k == 1) { for (p = b, h = 0; mon_name[dr[1] - 1][h]; h++) *p++ = mon_name[dr[1] - 1][h]; *p = 0; }
        else { p = put_num(b, dr[k], k == 2 ? 4 : 2); *p = 0; }
        font_draw(cx + 66 + (40 - font_width(b, FONT_REGULAR)) / 2, cy + 4, b, t->text, FONT_REGULAR);
        ui_button(cx + 106, cy, 22, 24, "+", 1, 0);
    }
    { i32 cx, cy; dt_stepper_pos(5, &cx, &cy); ui_button(x + cx + 6, y + cy - 2, 100, 28, tr("Set"), 1, 1); }
}


/* ---------- Language & Region ---------- */
#define LG_A_Y 100
#define LG_B_Y (LG_A_Y + ROW_H * 2 + 44)
static void lg_row(i32 x, i32 y, const char *name, int on)
{
    const theme_t *t = theme();
    font_draw(x + GRP_X + 16, y + 13, name, t->text, FONT_REGULAR);
    i32 cx = x + GRP_X + GRP_W - 30, cy = y + ROW_H / 2;
    fb_fill_circle(cx, cy, 9, on ? theme_accent() : t->sub);
    fb_fill_circle(cx, cy, on ? 4 : 7, on ? RGB(255, 255, 255) : t->card);
}
static void draw_lang(i32 x, i32 y)
{
    const theme_t *t = theme();
    heading(x, y, tr("Language & Region"), tr("Language of menus, apps and Settings"));
    font_draw(x + GRP_X, y + LG_A_Y - 20, tr("Interface language"), t->sub, FONT_REGULAR);
    ui_group(x + GRP_X, y + LG_A_Y, GRP_W, ROW_H * 2);
    lg_row(x, y + LG_A_Y, "English", lang_get() == 0);
    ui_hline(x + GRP_X + 16, y + LG_A_Y + ROW_H, GRP_W - 32);
    lg_row(x, y + LG_A_Y + ROW_H, "Русский", lang_get() == 1);
    font_draw(x + GRP_X, y + LG_B_Y - 20, tr("Keyboard layout at startup"), t->sub, FONT_REGULAR);
    ui_group(x + GRP_X, y + LG_B_Y, GRP_W, ROW_H * 2);
    lg_row(x, y + LG_B_Y, tr("English (US)"), !kbd_layout());
    ui_hline(x + GRP_X + 16, y + LG_B_Y + ROW_H, GRP_W - 32);
    lg_row(x, y + LG_B_Y + ROW_H, tr("Russian (JCUKEN)"), kbd_layout());
    font_draw(x + GRP_X, y + LG_B_Y + ROW_H * 2 + 14, tr("Switch the layout any time: Alt+Shift or Win+Space."), t->sub, FONT_REGULAR);
}

/* ---------- Software Update ---------- */
static int up_shown, up_spin, up_drawn = -1;
#define UP_BTN_W 150
#define UP_BTN_Y 262
#define UP_GRP_Y 340

static int up_busy(void) { int s = update_state(); return s == UP_CHECKING || s == UP_DOWNLOADING || s == UP_INSTALLING; }
int settings_animating(void) { return (page == PAGE_WIFI && (wifi_state() == WIFI_STARTING || wifi_busy())) || (page == PAGE_UPDATE && (up_busy() || up_drawn != update_state())) || (page == PAGE_DATETIME && ts_state() == TS_BUSY); }

static void up_center(i32 cx, i32 y, const char *s, u32 c, int font) { font_draw(cx - font_width(s, font) / 2, y, s, c, font); }

static void draw_update(i32 x, i32 y)
{
    const theme_t *t = theme();
    heading(x, y, "Software Update", "Keep EndixOS up to date");
    int s = update_state(); up_drawn = s;
    i32 cx = x + GRP_X + GRP_W / 2;

    /* значок: серая шестерёнка-плитка, при обновлении красный бейдж */
    i32 ix = cx - 36, iy = y + 84;
    for (i32 j = 0; j < 72; j++) fb_fill_round_rect(ix, iy + j, 72, 1, 0, RGB(168 - j / 3, 168 - j / 3, 176 - j / 3), 255);
    fb_fill_round_rect(ix, iy, 72, 72, 16, RGB(150, 150, 158), 255);
    fb_fill_round_rect(ix + 1, iy + 1, 70, 70, 15, RGB(176, 176, 184), 255);
    fb_fill_circle(ix + 36, iy + 36, 22, RGB(72, 72, 80));
    for (int k = 0; k < 8; k++) {                                   /* зубцы */
        static const i8 dx[8] = { 0, 17, 24, 17, 0, -17, -24, -17 }, dy[8] = { -24, -17, 0, 17, 24, 17, 0, -17 };
        fb_fill_circle(ix + 36 + dx[k], iy + 36 + dy[k], 6, RGB(72, 72, 80));
    }
    fb_fill_circle(ix + 36, iy + 36, 11, RGB(176, 176, 184));
    fb_fill_circle(ix + 36, iy + 36, 7, RGB(72, 72, 80));
    if (s == UP_AVAILABLE || s == UP_READY) {
        fb_fill_circle(ix + 64, iy + 8, 12, RGB(255, 59, 48));
        font_draw(ix + 64 - font_width("1", FONT_BOLD) / 2, iy + 1, "1", RGB(255, 255, 255), FONT_BOLD);
    }
    if (s == UP_CHECKING) {                                         /* крутящийся индикатор */
        up_spin++;
        for (int k = 0; k < 8; k++) {
            static const i8 sx[8] = { 0, 7, 10, 7, 0, -7, -10, -7 }, sy[8] = { -10, -7, 0, 7, 10, 7, 0, -7 };
            int a = 40 + ((8 + k - (up_spin / 2) % 8) % 8) * 26;
            fb_fill_circle(cx + sx[k], iy + 88 + 20 + sy[k], 2, RGB(a, a, a + 6 > 255 ? 255 : a + 6));
        }
    }

    char b[64], c[40]; char *p;
    i32 ty = y + 172;
    switch (s) {
    case UP_CHECKING:   up_center(cx, ty, "Checking for Updates...", t->text, FONT_BOLD); break;
    case UP_UPTODATE: {
        p = put_str(b, "EndixOS "); p = put_str(p, ENDIX_VERSION); put_str(p, " is up to date");
        up_center(cx, ty, b, t->text, FONT_BOLD);
        int sec = update_checked_secs(wm_get_ticks());
        p = put_str(c, "Build "); p = put_u32(p, ENDIX_BUILD); p = put_str(p, " \xC2\xB7 checked ");
        if (sec < 60) put_str(p, "just now"); else { p = put_u32(p, (u32)(sec / 60)); put_str(p, " min ago"); }
        up_center(cx, ty + 22, c, t->sub, FONT_REGULAR);
        break;
    }
    case UP_AVAILABLE:
        p = put_str(b, "EndixOS "); p = put_str(p, update_version()); put_str(p, " is available");
        up_center(cx, ty, b, t->text, FONT_BOLD);
        p = put_u32(c, update_size() / 1024 / 1024); p = put_str(p, "."); p = put_u32(p, update_size() / 1024 / 103 % 10); put_str(p, " MB");
        up_center(cx, ty + 22, c, t->sub, FONT_REGULAR);
        break;
    case UP_DOWNLOADING:
    case UP_INSTALLING:
        up_center(cx, ty, s == UP_DOWNLOADING ? "Downloading..." : "Installing...", t->text, FONT_BOLD);
        up_shown += (update_percent() - up_shown + (update_percent() > up_shown ? 3 : 0)) / 4;
        if (up_shown > update_percent()) up_shown = update_percent();
        fb_fill_round_rect(cx - 120, ty + 34, 240, 6, 3, theme_is_dark() ? RGB(80, 80, 88) : RGB(214, 214, 220), 255);
        if (up_shown > 0) fb_fill_round_rect(cx - 120, ty + 34, 240 * up_shown / 100 < 6 ? 6 : 240 * up_shown / 100, 6, 3, theme_accent(), 255);
        put_str(put_u32(b, (u32)update_percent()), "%");
        up_center(cx, ty + 50, b, t->sub, FONT_REGULAR);
        break;
    case UP_READY:      up_center(cx, ty, "Update installed", t->text, FONT_BOLD); up_center(cx, ty + 22, "Restart EndixOS to finish.", t->sub, FONT_REGULAR); break;
    case UP_ERROR:      up_center(cx, ty, "Update failed", t->text, FONT_BOLD); {
            const char *m = update_message(); char l1[64]; int n = 0;                       /* сообщение в две строки */
            for (; m[n] && n < 60; n++) l1[n] = m[n];
            if (m[n]) { while (n > 0 && m[n] != ' ') n--; }
            for (int k = 0; k < n; k++) l1[k] = m[k];
            l1[n] = 0;
            up_center(cx, ty + 22, l1, t->sub, FONT_REGULAR);
            if (m[n]) up_center(cx, ty + 40, m + n + 1, t->sub, FONT_REGULAR);
        } break;
    default:
        p = put_str(b, "EndixOS "); put_str(p, ENDIX_VERSION);
        up_center(cx, ty, b, t->text, FONT_BOLD);
        up_center(cx, ty + 22, "Check for Updates to see if a new version is available.", t->sub, FONT_REGULAR);
    }

    /* кнопка */
    const char *bl = 0; int prim = 0;
    if (s == UP_AVAILABLE) { bl = "Update Now"; prim = update_can_install(); }
    else if (s == UP_READY) { bl = "Restart Now"; prim = 1; }
    else if (s == UP_ERROR) bl = "Try Again";
    else if (!up_busy()) bl = "Check for Updates";
    if (bl) ui_button(cx - UP_BTN_W / 2, y + UP_BTN_Y, UP_BTN_W, 30, bl, !(s == UP_AVAILABLE && !update_can_install()), prim);
    if (s == UP_AVAILABLE && !update_can_install())
        up_center(cx, y + UP_BTN_Y + 40, "Install EndixOS to disk to receive updates.", t->sub, FONT_REGULAR);

    /* карточка с настройкой */
    ui_group(x + GRP_X, y + UP_GRP_Y, GRP_W, ROW_H * 2);
    font_draw(x + GRP_X + 16, y + UP_GRP_Y + 13, "Automatically check for updates", t->text, FONT_REGULAR);
    ui_switch(x + GRP_X + GRP_W - 56, y + UP_GRP_Y + 10, update_auto());
    ui_hline(x + GRP_X + 16, y + UP_GRP_Y + ROW_H, GRP_W - 16);
    font_draw(x + GRP_X + 16, y + UP_GRP_Y + ROW_H + 13, "Installed version", t->text, FONT_REGULAR);
    p = put_str(b, ENDIX_VERSION); p = put_str(p, " (build "); p = put_u32(p, ENDIX_BUILD); put_str(p, ")");
    font_draw(x + GRP_X + GRP_W - 16 - font_width(b, FONT_REGULAR), y + UP_GRP_Y + ROW_H + 13, b, t->sub, FONT_REGULAR);
}

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

    /* поле поиска */
    fb_fill_round_rect(x + 10, y + 10, SIDEBAR_W - 20, 26, 8, theme_is_dark() ? RGB(255, 255, 255) : RGB(0, 0, 0), theme_is_dark() ? 26 : 12);
    if (sq_focus) {
        fb_fill_round_rect(x + 9, y + 9, SIDEBAR_W - 18, 28, 9, theme_accent(), 120);
        fb_fill_round_rect(x + 11, y + 11, SIDEBAR_W - 22, 24, 7, t->card, 255);
    }
    fb_fill_circle(x + 24, y + 22, 5, t->sub); fb_fill_circle(x + 24, y + 22, 3, sq_focus ? t->card : t->side);   /* лупа */
    for (int k = 0; k < 4; k++) fb_fill_rect(x + 27 + k, y + 25 + k, 2, 2, t->sub);
    if (sqn) {
        i32 tx = font_draw(x + 38, y + 15, sq, t->text, FONT_REGULAR);
        if (sq_focus) fb_fill_rect(tx + 1, y + 16, 1, 15, t->text);
    } else {
        font_draw(x + 38, y + 15, tr("Search"), t->sub, FONT_REGULAR);
        if (sq_focus) fb_fill_rect(x + 37, y + 16, 1, 15, t->text);
    }

    /* учётная запись */
    avatar_draw(x + 36, y + 66, 17);
    { const char *un = login_user(); font_draw(x + 62, y + 50, un && un[0] ? un : "endix", t->text, FONT_BOLD); }
    font_draw(x + 62, y + 68, "EndixOS user", t->sub, FONT_REGULAR);

    int vp[PAGE_COUNT], nv = vis_pages(vp);
    if (!nv) font_draw(x + 18, y + SIDE_ITEMS_Y + 6, tr("No Results"), t->sub, FONT_REGULAR);
    for (int v = 0; v < nv; v++) {
        int i = vp[v];
        i32 ix = x + 10, iy = y + SIDE_ITEMS_Y + v * SIDE_PITCH;
        int sel = (i == page);
        if (sel)
            fb_fill_round_rect(ix, iy, SIDEBAR_W - 20, 26, 7, theme_accent(), 255);
        if (i == PAGE_ABOUT) {
            fb_fill_round_rect(ix + 6, iy + 4, 20, 20, 6, sel ? RGB(255, 255, 255) : theme_accent(), 255);
            logo_draw(ix + 9, iy + 7, 14, sel ? theme_accent() : RGB(255, 255, 255));
        } else {
            if (i == PAGE_DISPLAY) ui_squircle_icon(G_APPEAR, ix + 6, iy + 4, 20, RGB(52, 120, 246));
            else if (i == PAGE_SOUND) ui_squircle_icon(G_MUSIC, ix + 6, iy + 4, 20, RGB(255, 69, 58));
            else if (i == PAGE_TRACKPAD) ui_squircle_icon(G_DISK, ix + 6, iy + 4, 20, RGB(142, 142, 147));
            else if (i == PAGE_DATETIME) icon_draw(ICON_APP_CLOCK, ix + 6, iy + 4, 20);
            else if (i == PAGE_LANG) ui_squircle_icon(G_INFO, ix + 6, iy + 4, 20, RGB(52, 199, 89));
            else if (i == PAGE_WIFI) ui_squircle_icon(G_INFO, ix + 6, iy + 4, 20, RGB(10, 132, 255));
            else if (i == PAGE_UPDATE) ui_squircle_icon(G_DOWN, ix + 6, iy + 4, 20, RGB(142, 142, 147));
            else if (i == PAGE_ICONS) icon_draw(ICON_LAUNCHPAD, ix + 6, iy + 4, 20);
            else if (i == PAGE_ACCOUNT) avatar_draw_as(avatar_get(), avatar_color(), ix + 16, iy + 14, 10);
            else icon_draw(i == PAGE_WALL ? ICON_TILE_WALL : i == PAGE_GLASS ? ICON_TILE_GLASS : ICON_TILE_APPEAR, ix + 6, iy + 4, 20);
        }
        font_draw(ix + 36, iy + 5, tr(page_name[i]), sel ? RGB(255, 255, 255) : t->text, FONT_REGULAR);
    }

    if (page != PAGE_DATETIME) dr_ok = 0;
    switch (page) {
    case PAGE_WALL:   draw_wallpapers(x, y); break;
    case PAGE_PHOTOS: draw_photos(x, y);     break;
    case PAGE_THEMES: draw_themes(x, y);     break;
    case PAGE_GLASS:  draw_glass(x, y);      break;
    case PAGE_ICONS:  draw_icons(x, y);      break;
    case PAGE_DISPLAY: draw_display(x, y);   break;
    case PAGE_SOUND:  draw_sound(x, y);      break;
    case PAGE_TRACKPAD: draw_trackpad(x, y); break;
    case PAGE_DATETIME: draw_datetime(x, y); break;
    case PAGE_LANG:   draw_lang(x, y);       break;
    case PAGE_WIFI:   draw_wifi(x, y);       break;
    case PAGE_UPDATE: draw_update(x, y);     break;
    case PAGE_ACCOUNT: draw_account(x, y);   break;
    case PAGE_ABOUT:  about_draw(x + SIDEBAR_W, y); break;
    }
}

int settings_click(i32 rx, i32 ry)
{
    sq_focus = in_rect(rx, ry, 10, 10, SIDEBAR_W - 20, 26);
    if (sq_focus) return 1;
    if (in_rect(rx, ry, 10, 46, SIDEBAR_W - 20, 40)) { page = PAGE_ACCOUNT; return 1; }       /* карточка пользователя */
    {
        int vp[PAGE_COUNT], nv = vis_pages(vp);
        for (int v = 0; v < nv; v++)
            if (in_rect(rx, ry, 10, SIDE_ITEMS_Y + v * SIDE_PITCH, SIDEBAR_W - 20, 26)) {
                page = vp[v];
                return 1;
            }
    }

    if (page == PAGE_WALL) {
        for (int i = 0, slot = 0; i < scenery_count() && i < WALL_RENDERED; i++) {
            if (scenery_hidden(i)) continue;
            i32 tx, ty;
            thumb_pos(slot++, &tx, &ty);
            if (in_rect(rx, ry, tx, ty, THUMB_W, THUMB_H)) {
                settings_set_wallpaper(WALL_COUNT + i);
                return 1;
            }
        }
    } else if (page == PAGE_DISPLAY || page == PAGE_SOUND) {
        if (in_rect(rx, ry, SL_X - 12, 84, SL_W + 24, ROW_H)) {
            sl_drag = page == PAGE_DISPLAY ? 20 : 21;
            settings_drag(rx);
            return 1;
        }
        if (page == PAGE_DISPLAY && in_rect(rx, ry, PERF_X, PERF_Y, PERF_W, ROW_H)) {
            int k = (rx - PERF_X) * PERF_MODES / PERF_W;
            perf_set_mode(k < 0 ? 0 : (k >= PERF_MODES ? PERF_MODES - 1 : k));
            return 1;
        }
        if (page == PAGE_DISPLAY)
            for (int i = 0; i < res_count(); i++)
                if (in_rect(rx, ry, GRP_X, RES_Y + i * RES_ROW, GRP_W, RES_ROW)) {
                    res_sel = i;
                    res_apply(i, &res_msg);
                    return 1;
                }
    } else if (page == PAGE_TRACKPAD) {
        int sp, tap, nat;
        ps2_tp_get(&sp, &tap, &nat);
        if (in_rect(rx, ry, SL_X - 12, 84, SL_W + 24, ROW_H)) { sl_drag = 22; settings_drag(rx); return 1; }
        if (in_rect(rx, ry, GRP_X, 84 + ROW_H, GRP_W, ROW_H)) { ps2_tp_set(sp, !tap, nat); return 1; }
        if (in_rect(rx, ry, GRP_X, 84 + 2 * ROW_H, GRP_W, ROW_H)) { ps2_tp_set(sp, tap, !nat); return 1; }
    } else if (page == PAGE_LANG) {
        if (in_rect(rx, ry, GRP_X, LG_A_Y, GRP_W, ROW_H)) { lang_set(0); return 1; }
        if (in_rect(rx, ry, GRP_X, LG_A_Y + ROW_H, GRP_W, ROW_H)) { lang_set(1); return 1; }
        if (in_rect(rx, ry, GRP_X, LG_B_Y, GRP_W, ROW_H)) { kbd_set_layout(0); return 1; }
        if (in_rect(rx, ry, GRP_X, LG_B_Y + ROW_H, GRP_W, ROW_H)) { kbd_set_layout(1); return 1; }
    } else if (page == PAGE_DATETIME) {
        dt_draft();
        if (in_rect(rx, ry, DT_TZ_PX - 96 - 26, DT_A_Y + ROW_H + 9, 26, 24)) { ts_set_tz(ts_tz() - 30); return 1; }
        if (in_rect(rx, ry, DT_TZ_PX, DT_A_Y + ROW_H + 9, 26, 24)) { ts_set_tz(ts_tz() + 30); return 1; }
        if (in_rect(rx, ry, GRP_X, DT_A_Y + 2 * ROW_H, GRP_W, ROW_H)) { ts_set_auto(!ts_auto()); return 1; }
        if (in_rect(rx, ry, GRP_X + GRP_W - 96, DT_S_Y + 6, 96, 28)) { ts_sync(); return 1; }
        for (int k = 0; k < 5; k++) {
            i32 cx, cy;
            dt_stepper_pos(k, &cx, &cy);
            if (in_rect(rx, ry, cx + 44, cy, 22, 24)) { dt_step(k, -1); return 1; }
            if (in_rect(rx, ry, cx + 106, cy, 22, 24)) { dt_step(k, 1); return 1; }
        }
        { i32 cx, cy; dt_stepper_pos(5, &cx, &cy);
          if (in_rect(rx, ry, cx + 6, cy - 2, 100, 28)) { ts_apply(dr[2], dr[1], dr[0], dr[3], dr[4]); dr_ok = 0; return 1; } }
    } else if (page == PAGE_PHOTOS) {
        for (int i = 0; i < photos_count(); i++) {
            i32 tx, ty;
            thumb_pos(i, &tx, &ty);
            if (in_rect(rx, ry, tx, ty, THUMB_W, THUMB_H)) {
                settings_set_wallpaper(WALL_PHOTO0 + i);
                return 1;
            }
        }
    } else if (page == PAGE_WIFI) {
        if (wifi_page_click(rx, ry)) return 1;
    } else if (page == PAGE_UPDATE) {
        i32 cx = GRP_X + GRP_W / 2;
        int s = update_state();
        if (in_rect(rx, ry, cx - UP_BTN_W / 2, UP_BTN_Y, UP_BTN_W, 30)) {
            if (s == UP_AVAILABLE) { if (update_can_install()) update_install(); }
            else if (s == UP_READY) update_restart();
            else if (!up_busy()) update_check();
            return 1;
        }
        if (in_rect(rx, ry, GRP_X, UP_GRP_Y, GRP_W, ROW_H)) { update_set_auto(!update_auto()); return 1; }
    } else if (page == PAGE_ACCOUNT) {
        i32 gx0 = GRP_X + (GRP_W - (AC_COLS * (AC_CELL + AC_GAP) - AC_GAP)) / 2;
        for (int k = 0; k <= AVATAR_EMOJI; k++)
            if (in_rect(rx, ry, gx0 + (k % AC_COLS) * (AC_CELL + AC_GAP), AC_GRID_Y + 10 + (k / AC_COLS) * (AC_CELL + AC_GAP), AC_CELL, AC_CELL)) { avatar_set(k); return 1; }
        for (int c = 0; c < AVATAR_COLORS; c++)
            if (in_rect(rx, ry, GRP_X + GRP_W - 24 - (AVATAR_COLORS - 1 - c) * 28 - 13, AC_COL_Y + AC_COL_H / 2 - 13, 26, 26)) { avatar_set_color(c); return 1; }
    } else if (page == PAGE_ABOUT) {
        if (in_rect(rx, ry, GRP_X, AB_UPD_Y, AB_UPD_W, 30)) { page = PAGE_UPDATE; return 1; }
    } else if (page == PAGE_ICONS) {
        if (in_rect(rx, ry, IC_SEG_X, IC_ROW_Y, IC_SEG_W, ROW_H)) {
            int k = (rx - IC_SEG_X) * ICON_STYLE_COUNT / IC_SEG_W;
            icon_set_style(k < 0 ? 0 : (k >= ICON_STYLE_COUNT ? ICON_STYLE_COUNT - 1 : k));
            return 1;
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

void settings_show_about(void) { page = PAGE_ABOUT; sqn = 0; sq[0] = 0; }
void settings_show_page(int p) { page = p == 1 ? PAGE_THEMES : p == 2 ? PAGE_GLASS : PAGE_WALL; sqn = 0; sq[0] = 0; }

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

    about_laptop(cx, y + 26);

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
    if (sl_drag == 20) cc_set_brightness(20 + v * 80 / 100);
    else if (sl_drag == 21) cc_set_volume(v);
    else if (sl_drag == 22) { int sp, tap, nat; ps2_tp_get(&sp, &tap, &nat); ps2_tp_set(v, tap, nat); }
    else if (sl_drag >= 2) fb_glass_param_set(sl_drag - 2, v);
    else wm_set_option(WM_OPT_DOCK_ZOOM, v);
}

void settings_release(void) { sl_drag = 0; }

int settings_key(i32 key)
{
    if (page == PAGE_WIFI && wifi_page_key(key)) return 1;
    if (!sq_focus) return 0;
    if (key == 27) { sqn = 0; sq[0] = 0; sq_focus = 0; }
    else if (key == '\b') { if (sqn) utf8_pop(sq, &sqn); }
    else if (key == '\n') {
        int vp[PAGE_COUNT], nv = vis_pages(vp);
        if (nv) page = vp[0];
        sq_focus = 0;
    } else if (is_text_cp(key)) { utf8_append(sq, &sqn, (int)sizeof(sq) - 1, key); }
    else return 0;
    return 1;
}
