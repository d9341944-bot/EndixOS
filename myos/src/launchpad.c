#include "launchpad.h"
#include "framebuffer.h"
#include "keyboard.h"
#include "settings.h"
#include "heap.h"
#include <stdint.h>

extern void dock_draw_icon(int id, int x, int y, int sz);
extern volatile uint32_t ticks;
extern void open_files(void);
extern void open_clock(void);
extern void settings_open(void);
extern void terminal_open(void);
extern void snake_open(void);
extern void open_about(void);
extern void run_cmatrix(void);
extern void cmd_reboot(void);

struct lp_app { const char* name; void (*launch)(void); int icon_id; };

static struct lp_app apps[] = {
    { "Files",    open_files,     0 },
    { "Clock",    open_clock,     1 },
    { "Settings", settings_open,  2 },
    { "Terminal", terminal_open,  3 },
    { "Snake",    snake_open,     4 },
    { "About",    open_about,     5 },
    { "Matrix",   run_cmatrix,    6 },
    { "Reboot",   cmd_reboot,     7 },
};
#define APP_COUNT 8
#define COLS 4

/* Буфер размытого фона */
static uint32_t* lp_bg = 0;
static int lp_w = 0, lp_h = 0;

/* Размытие + затемнение */
static void blur_and_darken(void) {
    int W = (int)fb_width();
    int H = (int)fb_height();
    int SW = W / 4, SH = H / 4;
    if (SW > 300) SW = 300;
    if (SH > 220) SH = 220;
    static uint32_t small[300 * 220];
    static uint32_t blur[300 * 220];

    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            uint32_t r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 4; dy++)
                for (int dx = 0; dx < 4; dx++) {
                    uint32_t c = fb_getpixel(x*4+dx, y*4+dy);
                    r += (c >> 16) & 0xFF; g += (c >> 8) & 0xFF; b += c & 0xFF;
                }
            small[y*SW+x] = ((r/16) << 16) | ((g/16) << 8) | (b/16);
        }

    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            uint32_t r = 0, g = 0, b = 0; int n = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x+dx, ny = y+dy;
                    if (nx < 0 || nx >= SW || ny < 0 || ny >= SH) continue;
                    uint32_t c = small[ny*SW+nx];
                    r += (c >> 16) & 0xFF; g += (c >> 8) & 0xFF; b += c & 0xFF;
                    n++;
                }
            blur[y*SW+x] = ((r/n) << 16) | ((g/n) << 8) | (b/n);
        }

    for (int y = 0; y < H; y++) {
        int sy = (y * SH) / H;
        for (int x = 0; x < W; x++) {
            int sx = (x * SW) / W;
            uint32_t c = blur[sy*SW+sx];
            uint32_t r = ((c >> 16) & 0xFF) * 55 / 100;
            uint32_t g = ((c >> 8) & 0xFF)  * 60 / 100;
            uint32_t b = (c & 0xFF)         * 70 / 100;
            fb_putpixel(x, y, (r << 16) | (g << 8) | b);
        }
    }
}

/* Одна иконка */
static void draw_one_icon(int id, const char* name, int cx, int cy, int sz, int focused) {
    int R = 16;

    /* Тень для focused */
    if (focused) {
        for (int py = cy - 12; py < cy + sz + 12; py++)
            for (int px = cx - 12; px < cx + sz + 12; px++) {
                if (px < 0 || py < 0 || px >= lp_w || py >= lp_h) continue;
                uint32_t c = fb_getpixel(px, py);
                uint32_t r = ((c >> 16) & 0xFF) * 6 / 10;
                uint32_t g = ((c >> 8) & 0xFF)  * 6 / 10;
                uint32_t b = (c & 0xFF)         * 6 / 10;
                fb_putpixel(px, py, (r << 16) | (g << 8) | b);
            }
    }

    /* Фон иконки — светло-серый rounded */
    for (int py = 0; py < sz; py++) {
        uint32_t t = (py * 255) / sz;
        uint32_t r = (230 * (255-t) + 195 * t) / 255;
        uint32_t g = (230 * (255-t) + 195 * t) / 255;
        uint32_t b = (240 * (255-t) + 210 * t) / 255;
        for (int px = 0; px < sz; px++) {
            int on = 1;
            int ccx = 0, ccy = 0;
            if (px < R && py < R)                        { ccx = R-1-px;    ccy = R-1-py; }
            else if (px >= sz-R && py < R)               { ccx = px-(sz-R); ccy = R-1-py; }
            else if (px < R && py >= sz-R)               { ccx = R-1-px;    ccy = py-(sz-R); }
            else if (px >= sz-R && py >= sz-R)           { ccx = px-(sz-R); ccy = py-(sz-R); }
            else goto ok;
            if (ccx*ccx + ccy*ccy > R*R) on = 0;
ok:
            if (on) fb_putpixel(cx + px, cy + py, (r<<16)|(g<<8)|b);
        }
    }

    /* Artwork */
    dock_draw_icon(id, cx, cy, sz);

    /* Подпись */
    int nl = 0; while (name[nl]) nl++;
    gfx_puts(cx + (sz - nl*8)/2, cy + sz + 12, name,
             focused ? 0xFFFFFF : 0xD0D0D8, 1);

    /* Только для фокусной иконки — 3-кольцевая белая рамка */
    if (focused) {
        for (int k = 0; k < 3; k++)
            gfx_rounded_outline(cx - 10 - k, cy - 10 - k,
                                sz + 20 + 2*k, sz + 20 + 2*k,
                                22 + k, 0xFFFFFF);
    }
}

static void draw_launchpad(int icon_sz, int gap, int start_x, int start_y,
                           int row_step, int sel) {
    int W = (int)fb_width();

    /* Восстановить фон из буфера */
    if (lp_bg) {
        for (int y = 0; y < lp_h; y++) {
            uint32_t* row = lp_bg + y * lp_w;
            for (int x = 0; x < lp_w; x++)
                fb_putpixel(x, y, row[x]);
        }
    }

    /* Поиск */
    int sw = 320;
    int sx = (W - sw) / 2;
    gfx_rounded_fill(sx, 40, sw, 30, 15, 0x30303A);
    gfx_rounded_outline(sx, 40, sw, 30, 15, 0x505058);
    gfx_puts(sx + 24, 51, "Search", 0x808090, 1);

    /* Заголовок */
    const char* t = "Applications";
    int tl = 0; while (t[tl]) tl++;
    gfx_puts((W - tl*8) / 2, 100, t, 0xFFFFFF, 2);

    /* Иконки */
    for (int i = 0; i < APP_COUNT; i++) {
        int col = i % COLS;
        int row = i / COLS;
        int cx = start_x + col * (icon_sz + gap);
        int cy = start_y + row * row_step;
        draw_one_icon(apps[i].icon_id, apps[i].name, cx, cy, icon_sz, i == sel);
    }

    /* Подсказка */
    gfx_puts((W - 49*8)/2, (int)fb_height() - 40,
             "arrows: navigate    ENTER: launch    ESC: close",
             0xC0C0D0, 1);
}

void run_launchpad(void) {
    lp_w = (int)fb_width();
    lp_h = (int)fb_height();

    /* 1. Размыть фон */
    blur_and_darken();

    /* 2. Сохранить размытый фон в буфер */
    lp_bg = (uint32_t*)kmalloc(lp_w * lp_h * 4);
    if (lp_bg) {
        for (int y = 0; y < lp_h; y++)
            for (int x = 0; x < lp_w; x++)
                lp_bg[y * lp_w + x] = fb_getpixel(x, y);
    }

    /* 3. Параметры сетки */
    int icon_sz = 112;
    int gap = 60;
    int total_w = COLS * icon_sz + (COLS - 1) * gap;
    int start_x = (lp_w - total_w) / 2;
    int start_y = 170;
    int row_step = icon_sz + 60;

    int sel = 0;
    draw_launchpad(icon_sz, gap, start_x, start_y, row_step, sel);
    gfx_flush();

    void (*to_launch)(void) = 0;

    while (1) {
        int c = keyboard_getchar();
        if (c == 27) break;
        else if (c == KEY_LEFT)  { if (sel > 0) sel--; }
        else if (c == KEY_RIGHT) { if (sel < APP_COUNT - 1) sel++; }
        else if (c == KEY_UP)    { if (sel >= COLS) sel -= COLS; }
        else if (c == KEY_DOWN)  { if (sel + COLS < APP_COUNT) sel += COLS; }
        else if (c == 10 || c == 32) { to_launch = apps[sel].launch; break; }
        else continue;

        draw_launchpad(icon_sz, gap, start_x, start_y, row_step, sel);
        gfx_flush();
    }

    if (lp_bg) { kfree(lp_bg); lp_bg = 0; }
    if (to_launch) to_launch();
}
