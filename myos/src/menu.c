#include "menu.h"
#include "framebuffer.h"
#include "settings.h"
#include "keyboard.h"

/* Меню в menu bar */
#define MENU_COUNT   4
static const char* menu_titles[MENU_COUNT] = { "File", "Edit", "View", "Help" };

/* Пункты каждого меню (максимум 6) */
#define MAX_ITEMS 6
static const char* menu_items[MENU_COUNT][MAX_ITEMS] = {
    { "New Terminal", "New Files", "Open Settings", "-", "Close Window", "Quit EndixOS" },
    { "Copy", "Paste", "Select All", 0 },
    { "Toggle Fullscreen", "Minimize All", "-", "Show Desktop", 0 },
    { "About EndixOS", "Keyboard Shortcuts", "-", "Restart", 0 },
};

static int menu_open_idx = -1;   /* -1 = закрыто */
static int menu_hover = 0;
static int menu_x[MENU_COUNT];
static int menu_w[MENU_COUNT];

/* Топбар x-позиции (согласованы с draw_topbar в kernel.c) */
static void calc_positions(void) {
    /* File Edit View Help — жёсткие позиции как в topbar */
    menu_x[0] = 110; menu_w[0] = 60;   /* File */
    menu_x[1] = 180; menu_w[1] = 60;   /* Edit */
    menu_x[2] = 250; menu_w[2] = 60;   /* View */
    menu_x[3] = 320; menu_w[3] = 60;   /* Help */
}

void menu_init(void) {
    calc_positions();
    menu_open_idx = -1;
    menu_hover = 0;
}

int menu_is_open(void) { return menu_open_idx >= 0; }

int menu_topbar_hit(int x, int y) {
    if (y < 0 || y >= 26) return -1;
    for (int i = 0; i < MENU_COUNT; i++) {
        if (x >= menu_x[i] && x < menu_x[i] + menu_w[i]) return i;
    }
    return -1;
}

void menu_open_by_index(int idx) {
    if (idx < 0 || idx >= MENU_COUNT) return;
    menu_open_idx = idx;
    menu_hover = 0;
    /* Пропустить "-" при открытии */
    while (menu_items[idx][menu_hover] &&
           menu_items[idx][menu_hover][0] == '-' &&
           menu_items[idx][menu_hover][1] == 0) {
        menu_hover++;
    }
}

void menu_close(void) { menu_open_idx = -1; }

void menu_draw(void) {
    if (menu_open_idx < 0) return;

    int idx = menu_open_idx;
    int mx = menu_x[idx];
    int my = 26;

    /* Высота dropdown */
    int n = 0;
    while (menu_items[idx][n]) n++;

    int mw = 200;
    int item_h = 24;
    int mh = n * item_h + 8;

    /* Тень */
    gfx_shadow(mx, my, mw, mh, 8, g_settings.bg);

    /* Фон */
    gfx_rounded_fill(mx, my, mw, mh, 8, g_settings.surface2);
    gfx_rounded_outline(mx, my, mw, mh, 8, g_settings.outline);

    for (int i = 0; i < n; i++) {
        int iy = my + 4 + i * item_h;
        int is_sel = (i == menu_hover);
        int is_sep = (menu_items[idx][i][0] == '-' && menu_items[idx][i][1] == 0);

        if (is_sep) {
            for (int px = 8; px < mw - 8; px++)
                fb_putpixel(mx + px, iy + item_h/2, g_settings.outline);
            continue;
        }

        if (is_sel)
            gfx_rounded_fill(mx + 4, iy, mw - 8, item_h, 6, g_settings.primary);

        gfx_puts(mx + 14, iy + 6, menu_items[idx][i],
                 is_sel ? 0x000000 : g_settings.on_surface, 1);
    }
}

/* Возвращает 1 если клавиша обработана */
int menu_handle_key(int c) {
    if (menu_open_idx < 0) return 0;

    int idx = menu_open_idx;
    int n = 0;
    while (menu_items[idx][n]) n++;

    if (c == 27) { menu_close(); return 1; }   /* ESC */

    if (c == KEY_UP) {
        /* Пропускаем разделители */
        for (int i = 0; i < n; i++) {
            menu_hover--;
            if (menu_hover < 0) menu_hover = n - 1;
            if (!(menu_items[idx][menu_hover][0] == '-' &&
                  menu_items[idx][menu_hover][1] == 0)) break;
        }
        return 1;
    }
    if (c == KEY_DOWN) {
        for (int i = 0; i < n; i++) {
            menu_hover++;
            if (menu_hover >= n) menu_hover = 0;
            if (!(menu_items[idx][menu_hover][0] == '-' &&
                  menu_items[idx][menu_hover][1] == 0)) break;
        }
        return 1;
    }
    if (c == KEY_LEFT)  { menu_open_idx--; if (menu_open_idx < 0) menu_open_idx = MENU_COUNT - 1; menu_hover = 0; return 1; }
    if (c == KEY_RIGHT) { menu_open_idx = (menu_open_idx + 1) % MENU_COUNT; menu_hover = 0; return 1; }

    if (c == '\n') {
        extern void menu_action(int menu_id, int item_id);
        menu_action(menu_open_idx, menu_hover);
        menu_close();
        return 1;
    }
    return 1;
}

void menu_click(int x, int y) {
    /* Клик в топбаре */
    int mh = menu_topbar_hit(x, y);
    if (mh >= 0) {
        if (menu_open_idx == mh) menu_close();
        else menu_open_by_index(mh);
        return;
    }
    /* Клик в открытом меню */
    if (menu_open_idx >= 0) {
        int idx = menu_open_idx;
        int mx = menu_x[idx], my = 26;
        int mw = 200, item_h = 24;
        int n = 0; while (menu_items[idx][n]) n++;
        int mhh = n * item_h + 8;
        if (x >= mx && x < mx + mw && y >= my && y < my + mh) {
            int item = (y - my - 4) / item_h;
            if (item >= 0 && item < n) {
                extern void menu_action(int menu_id, int item_id);
                menu_action(menu_open_idx, item);
            }
        }
        menu_close();
        return;
    }
    menu_close();
}

void menu_draw_underlines(void) {
    /* Ничего — просто заглушка */
}

/* Экспортируем x-позиции для topbar, чтобы совпадали */
int menu_x_start(int idx) { calc_positions(); return menu_x[idx]; }
int menu_x_width(int idx) { calc_positions(); return menu_w[idx]; }
