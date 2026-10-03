#include "wm.h"
#include "framebuffer.h"
#include "settings.h"
#include "anim.h"
#include <stdint.h>

static wm_window wins[WM_MAX_WIN];
static int focused = -1;
static int minimized[WM_MAX_WIN] = {0};
static int wm_anim_active = 0;
static uint32_t win_anim_t0[WM_MAX_WIN] = {0};
extern volatile uint32_t ticks;

static void strcopy(char* d, const char* s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

void wm_init(void) {
    for (int i = 0; i < WM_MAX_WIN; i++) { wins[i].used = 0; minimized[i] = 0; }
    focused = -1;
}

int wm_create(const char* title, int x, int y, int w, int h, wm_draw_fn fn, void* user) {
    for (int i = 0; i < WM_MAX_WIN; i++) {
        if (!wins[i].used) {
            wins[i].used = 1;
            wins[i].x = x; wins[i].y = y;
            wins[i].w = w; wins[i].h = h;
            wins[i].border_color = 0x3A3A3A;
            wins[i].title_color  = 0x2E2E2E;
            wins[i].bg_color     = 0x252525;
            wins[i].draw = fn;
            wins[i].user = user;
            strcopy(wins[i].title, title, WM_TITLE_MAX);
            win_anim_t0[i] = ticks;
            wm_anim_active = 1;
            focused = i;
            return i;
        }
    }
    return -1;
}

void wm_close(int id) {
    if (id < 0 || id >= WM_MAX_WIN || !wins[id].used) return;
    wins[id].used = 0;
    if (focused == id) {
        focused = -1;
        for (int i = 0; i < WM_MAX_WIN; i++)
            if (wins[i].used && !minimized[i]) { focused = i; break; }
    }
}

void wm_raise(int id) { if (id >= 0 && id < WM_MAX_WIN && wins[id].used) focused = id; }

void wm_focus_next(void) {
    for (int i = 1; i <= WM_MAX_WIN; i++) {
        int idx = (focused + i + WM_MAX_WIN) % WM_MAX_WIN;
        if (wins[idx].used && !minimized[idx]) { focused = idx; return; }
    }
}
void wm_focus_prev(void) { wm_focus_next(); }
int wm_focused(void) { return focused; }

void wm_move_focused(int dx, int dy) {
    if (focused < 0) return;
    wins[focused].x += dx;
    wins[focused].y += dy;
    if (wins[focused].x < 0) wins[focused].x = 0;
    if (wins[focused].y < 30) wins[focused].y = 30;
}

void wm_clear_all(void) { for (int i = 0; i < WM_MAX_WIN; i++) wins[i].used = 0; focused = -1; }
int wm_is_used(int id) { return (id >= 0 && id < WM_MAX_WIN) ? wins[id].used : 0; }
int wm_is_focused(int id) { return (id == focused); }
const char* wm_title(int id) { return (id >= 0 && id < WM_MAX_WIN) ? wins[id].title : ""; }
int wm_is_minimized(int id) { return (id >= 0 && id < WM_MAX_WIN) ? minimized[id] : 0; }

static void draw_window(int i) {
    wm_window* w = &wins[i];
    int focused_this = (i == focused);

    /* Учёт fade-in: 0.0..1.0 */
    extern volatile uint32_t ticks;
    float alpha = 1.0f;
    uint32_t elapsed = ticks - win_anim_t0[i];
    if (elapsed < 15) {
        alpha = (float)elapsed / 15.0f;   /* 0 → 1 за 150 мс */
    }
    int blend = (int)(alpha * 100);        /* 0..100 */

    /* Хелпер для затемнения цвета */
    #define BLENDC(c) ( \
        ((uint32_t)((((c) >> 16) & 0xFF) * blend / 100) << 16) | \
        ((uint32_t)((((c) >> 8)  & 0xFF) * blend / 100) << 8)  | \
         (uint32_t)( ( (c)        & 0xFF) * blend / 100) )
    int TITLE_H = 36;
    int R = 12;

    int real_w = w->w;
    int real_h = w->h;
    int real_x = w->x;
    int real_y = w->y;

    /* Тень */
    for (int s = 5; s >= 1; s--) {
        uint32_t c = (s <= 2) ? 0x1A1A1E : 0x0E0E12;
        gfx_rounded_fill(real_x - s, real_y + s, real_w + s*2, real_h + s*2, R + s, c);
    }

    /* Тело окна */
    gfx_rounded_fill(real_x, real_y, real_w, real_h, R, g_settings.surface);

    /* Заголовок */
    uint32_t hb = focused_this ? 0x32323A : 0x25252C;
    gfx_rounded_fill(real_x, real_y, real_w, TITLE_H, R, hb);
    for (int py = TITLE_H - R; py < TITLE_H; py++)
        for (int px = R; px < real_w - R; px++)
            fb_putpixel(real_x + px, real_y + py, hb);

    for (int px = R; px < real_w - R; px++)
        fb_putpixel(real_x + px, real_y + TITLE_H, 0x1A1A1E);

    gfx_rounded_outline(real_x, real_y, real_w, real_h, R,
                        focused_this ? 0x50505A : 0x30303A);

    /* Traffic lights */
    int lx = real_x + 20;
    int ly = real_y + TITLE_H/2;

    for (int dy = -7; dy <= 7; dy++)
        for (int dx = -7; dx <= 7; dx++)
            if (dx*dx + dy*dy <= 49)
                fb_putpixel(lx + dx, ly + dy, focused_this ? 0xFF5F57 : 0x504044);

    for (int dy = -7; dy <= 7; dy++)
        for (int dx = -7; dx <= 7; dx++)
            if (dx*dx + dy*dy <= 49)
                fb_putpixel(lx + 22 + dx, ly + dy, focused_this ? 0xFEBC2E : 0x504838);

    for (int dy = -7; dy <= 7; dy++)
        for (int dx = -7; dx <= 7; dx++)
            if (dx*dx + dy*dy <= 49)
                fb_putpixel(lx + 44 + dx, ly + dy, focused_this ? 0x28C840 : 0x404840);

    if (focused_this) {
        for (int d = -3; d <= 3; d++) {
            fb_putpixel(lx + d, ly + d, 0x800000);
            fb_putpixel(lx + d, ly - d, 0x800000);
        }
        for (int d = -3; d <= 3; d++)
            fb_putpixel(lx + 22 + d, ly, 0x805000);
        for (int d = -3; d <= 3; d++) {
            fb_putpixel(lx + 44 + d, ly, 0x005000);
            fb_putpixel(lx + 44, ly + d, 0x005000);
        }
    }

    /* Название */
    int tl = 0; while (w->title[tl]) tl++;
    gfx_puts(real_x + (real_w - tl*8)/2, real_y + TITLE_H/2 - 4, w->title,
             focused_this ? 0xFFFFFF : 0xB0B0B8, 1);

    /* Контент — ВСЕГДА */
    if (w->draw) {
        w->draw(real_x + 2, real_y + TITLE_H + 2,
                real_w - 4, real_h - TITLE_H - 4, w->user);
    }
}











void wm_render(void) {
    for (int i = 0; i < WM_MAX_WIN; i++)
        if (wins[i].used && !minimized[i] && i != focused) draw_window(i);
    if (focused >= 0 && wins[focused].used && !minimized[focused])
        draw_window(focused);
    gfx_flush();
}

int wm_hit_test(int x, int y) {
    for (int i = WM_MAX_WIN - 1; i >= 0; i--) {
        if (!wins[i].used || minimized[i]) continue;
        if (x >= wins[i].x && x < wins[i].x + wins[i].w &&
            y >= wins[i].y && y < wins[i].y + wins[i].h) return i;
    }
    return -1;
}

int wm_close_button_hit(int x, int y) {
    for (int i = WM_MAX_WIN - 1; i >= 0; i--) {
        if (!wins[i].used || minimized[i]) continue;
        int bx = wins[i].x + wins[i].w - 26;
        int by = wins[i].y + 4;
        if (x >= bx && x < bx + 22 && y >= by && y < by + 22) return i;
    }
    return -1;
}
int wm_minimize_button_hit(int x, int y) { (void)x;(void)y; return -1; }
int wm_maximize_button_hit(int x, int y) { (void)x;(void)y; return -1; }
void wm_minimize(int id) {
    if (id < 0 || id >= WM_MAX_WIN) return;
    minimized[id] = 1;
    if (focused == id) {
        focused = -1;
        for (int i = 0; i < WM_MAX_WIN; i++)
            if (wins[i].used && !minimized[i]) { focused = i; break; }
    }
}
void wm_maximize(int id) { (void)id; }


int wm_animation_active(void) {
    extern volatile uint32_t ticks;
    for (int i = 0; i < WM_MAX_WIN; i++) {
        if (!wins[i].used) continue;
        if (ticks - win_anim_t0[i] < 15) return 1;   /* 150 мс */
    }
    wm_anim_active = 0;
    return 0;
}

