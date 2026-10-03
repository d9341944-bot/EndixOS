#include "snake.h"
#include "wm.h"
#include "framebuffer.h"
#include "settings.h"
#include "keyboard.h"
#include <stdint.h>

#define GRID_W 30
#define GRID_H 20
#define CELL   16

#define MAX_LEN (GRID_W * GRID_H)

static int snake_x[MAX_LEN], snake_y[MAX_LEN];
static int snake_len = 3;
static int dir_x = 1, dir_y = 0;
static int next_dir_x = 1, next_dir_y = 0;
static int food_x = 15, food_y = 10;
static int score = 0;
static int game_over = 0;
static int paused = 0;
static uint32_t last_move_tick = 0;
static int win_id = -1;

static uint32_t rng_state = 12345;
static uint32_t rng_next(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return rng_state;
}

static void snake_reset(void) {
    snake_len = 3;
    snake_x[0] = 10; snake_y[0] = 10;
    snake_x[1] = 9;  snake_y[1] = 10;
    snake_x[2] = 8;  snake_y[2] = 10;
    dir_x = 1; dir_y = 0;
    next_dir_x = 1; next_dir_y = 0;
    score = 0;
    game_over = 0;
    paused = 0;
    food_x = 15; food_y = 10;
    last_move_tick = 0;
}

static void place_food(void) {
    for (int tries = 0; tries < 100; tries++) {
        int fx = rng_next() % GRID_W;
        int fy = rng_next() % GRID_H;
        int ok = 1;
        for (int i = 0; i < snake_len; i++)
            if (snake_x[i] == fx && snake_y[i] == fy) { ok = 0; break; }
        if (ok) { food_x = fx; food_y = fy; return; }
    }
}

/* Вызывается из timer_cb (100 Гц) */
void snake_tick(void) {
    extern volatile uint32_t ticks;

    if (win_id < 0) return;
    if (!wm_is_used(win_id)) return;
    if (game_over || paused) return;

    /* Двигаем раз в 8 тиков PIT = 12.5 Гц */
    static uint32_t last = 0;
    static uint32_t dbg = 0;
    if (ticks < last) { last = ticks; return; }
    if (ticks - last < 8) return;
    last = ticks;

    /* Debug: пишем в serial каждые 100 движений */
    dbg++;
    if (dbg % 50 == 0) {
        extern void serial_puts(const char*);
        serial_puts("[snake] tick\n");
    }

    dir_x = next_dir_x;
    dir_y = next_dir_y;

    int nx = snake_x[0] + dir_x;
    int ny = snake_y[0] + dir_y;

    if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) {
        game_over = 1;
        wm_render();
        return;
    }
    for (int i = 0; i < snake_len - 1; i++) {
        if (snake_x[i] == nx && snake_y[i] == ny) {
            game_over = 1;
            wm_render();
            return;
        }
    }

    for (int i = snake_len - 1; i > 0; i--) {
        snake_x[i] = snake_x[i-1];
        snake_y[i] = snake_y[i-1];
    }
    snake_x[0] = nx;
    snake_y[0] = ny;

    if (nx == food_x && ny == food_y) {
        score++;
        if (snake_len < MAX_LEN) snake_len++;
        place_food();
    }

    wm_render();
}



static void snake_draw(int x, int y, int w, int h, void* user) {
    (void)user;
    (void)w; (void)h;

    /* Игровое поле */
    int field_w = GRID_W * CELL;
    int field_h = GRID_H * CELL;
    int fx = x + 4;
    int fy = y + 4;

    gfx_box_fill(fx, fy, field_w, field_h, 0x0A0A12);

    /* Сетка — тонкая */
    for (int i = 0; i <= GRID_W; i++)
        for (int j = 0; j < field_h; j++)
            fb_putpixel(fx + i * CELL, fy + j, 0x181828);
    for (int j = 0; j <= GRID_H; j++)
        for (int i = 0; i < field_w; i++)
            fb_putpixel(fx + i, fy + j * CELL, 0x181828);

    /* Еда */
    gfx_box_fill(fx + food_x * CELL + 3, fy + food_y * CELL + 3,
                 CELL - 6, CELL - 6, 0xFF4080);
    /* Блик */
    gfx_box_fill(fx + food_x * CELL + 5, fy + food_y * CELL + 5,
                 3, 3, 0xFFFFFF);

    /* Змейка */
    for (int i = 0; i < snake_len; i++) {
        uint32_t c = (i == 0) ? 0x80E080 : 0x40A040;
        gfx_box_fill(fx + snake_x[i] * CELL + 2, fy + snake_y[i] * CELL + 2,
                     CELL - 4, CELL - 4, c);
    }

    /* Глаза на голове */
    if (snake_len > 0 && !game_over) {
        int hx = fx + snake_x[0] * CELL;
        int hy = fy + snake_y[0] * CELL;
        gfx_box_fill(hx + 4, hy + 4, 2, 2, 0x000000);
        gfx_box_fill(hx + CELL - 6, hy + 4, 2, 2, 0x000000);
    }

    /* Score внизу */
    char sc[32];
    int n = 0;
    sc[n++]='s'; sc[n++]='c'; sc[n++]='o'; sc[n++]='r'; sc[n++]='e'; sc[n++]=':'; sc[n++]=' ';
    if (score == 0) sc[n++] = '0';
    else {
        char t[8]; int m = 0;
        int v = score;
        while (v > 0) { t[m++] = '0' + (v % 10); v /= 10; }
        while (m > 0) sc[n++] = t[--m];
    }
    sc[n] = 0;
    gfx_puts(fx, fy + field_h + 4, sc, 0x80E080, 1);

    if (game_over) {
        int gw = 160, gh = 60;
        int gx = fx + (field_w - gw) / 2;
        int gy = fy + (field_h - gh) / 2;
        gfx_rounded_fill(gx, gy, gw, gh, 10, 0x200000);
        gfx_puts(gx + 20, gy + 12, "GAME OVER", 0xFF4040, 2);
        gfx_puts(gx + 30, gy + 38, "Press R to restart", 0xC0C0C0, 1);
    }
    if (paused && !game_over) {
        gfx_puts(fx + field_w / 2 - 30, fy + field_h / 2 - 4,
                 "PAUSED", 0xFFFF00, 2);
    }
}

void snake_open(void) {
    if (win_id >= 0 && wm_is_used(win_id)) {
        wm_raise(win_id);
        wm_render();
        return;
    }
    snake_reset();
    int w = GRID_W * CELL + 12;
    int h = GRID_H * CELL + 36;
    int sx = ((int)fb_width() - w) / 2;
    int sy = ((int)fb_height() - h) / 2 - 20;
    win_id = wm_create("Snake", sx, sy, w, h, snake_draw, 0);
    wm_render();
}

void snake_handle_key(int c) {
    if (win_id < 0 || !wm_is_used(win_id)) return;

    /* Направления — не разворачиваться на 180 */
    if (c == KEY_UP && dir_y == 0)    { next_dir_x = 0; next_dir_y = -1; return; }
    if (c == KEY_DOWN && dir_y == 0)  { next_dir_x = 0; next_dir_y = 1;  return; }
    if (c == KEY_LEFT && dir_x == 0)  { next_dir_x = -1; next_dir_y = 0; return; }
    if (c == KEY_RIGHT && dir_x == 0) { next_dir_x = 1; next_dir_y = 0;  return; }

    if (c == 'r' || c == 'R')  { snake_reset(); wm_render(); return; }
    if (c == 'p' || c == 'P')  { paused = !paused; wm_render(); return; }
}

int snake_win_id(void) { return win_id; }
int snake_is_open(void) { return (win_id >= 0 && wm_is_used(win_id)); }
