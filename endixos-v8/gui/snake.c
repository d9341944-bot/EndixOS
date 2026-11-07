/* Игра Snake: логика на таймере PIT и отрисовка в окне. */
#include "snake.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "../kernel/event.h"
#include "../kernel/string.h"

#define CELL       SNAKE_CELL
#define MAXLEN     (SNAKE_W * SNAKE_H)
#define BOARD_W    (SNAKE_W * CELL)
#define BOARD_H    (SNAKE_H * CELL)
#define STEP_START 14          /* тиков PIT между шагами в начале (140 мс) */
#define STEP_MIN   6           /* предел скорости (60 мс) */

enum { ST_READY, ST_PLAY, ST_PAUSED, ST_OVER };

typedef struct { i8 x, y; } cell_t;

static cell_t body[MAXLEN];    /* body[0] - голова, body[len-1] - хвост */
static int    len;
static int    dir;             /* 0 вверх, 1 вправо, 2 вниз, 3 влево */
static int    queue[2], qn;    /* очередь поворотов: два быстрых нажатия не теряются */
static int    state;
static int    won;
static cell_t food;
static u32    score, best;
static u32    last_step, now;
static int    step_ticks;
static u32    rng = 2463534242u;

static const i8 dxs[4] = { 0, 1, 0, -1 };
static const i8 dys[4] = { -1, 0, 1, 0 };

static u32 rnd(void)           /* xorshift32 */
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int on_snake(int x, int y)
{
    for (int i = 0; i < len; i++)
        if (body[i].x == x && body[i].y == y)
            return 1;
    return 0;
}

static void place_food(void)
{
    if (len >= MAXLEN) {       /* поле заполнено: победа */
        won = 1;
        state = ST_OVER;
        return;
    }
    int pick = (int)(rnd() % (u32)(MAXLEN - len));
    for (int y = 0; y < SNAKE_H; y++) {
        for (int x = 0; x < SNAKE_W; x++) {
            if (on_snake(x, y))
                continue;
            if (pick-- == 0) {
                food.x = (i8)x;
                food.y = (i8)y;
                return;
            }
        }
    }
}

void snake_reset(void)
{
    int hx = SNAKE_W / 2, hy = SNAKE_H / 2;
    len = 3;
    for (int i = 0; i < len; i++) {
        body[i].x = (i8)(hx - i);
        body[i].y = (i8)hy;
    }
    dir = 1;
    qn = 0;
    score = 0;
    won = 0;
    step_ticks = STEP_START;
    state = ST_READY;
    rng ^= now * 2654435761u + 1;
    if (!rng)
        rng = 1;
    place_food();
}

static void start(void)
{
    state = ST_PLAY;
    last_step = now;
}

static void die(void)
{
    state = ST_OVER;
    if (score > best)
        best = score;
}

static void step(void)
{
    if (qn) {
        dir = queue[0];
        queue[0] = queue[1];
        qn--;
    }

    int nx = body[0].x + dxs[dir];
    int ny = body[0].y + dys[dir];
    if (nx < 0 || ny < 0 || nx >= SNAKE_W || ny >= SNAKE_H) {
        die();
        return;
    }

    int eat = (nx == food.x && ny == food.y);
    int check = eat ? len : len - 1;       /* хвост освобождает клетку, если не растём */
    for (int i = 0; i < check; i++) {
        if (body[i].x == nx && body[i].y == ny) {
            die();
            return;
        }
    }

    memmove(&body[1], &body[0], (size_t)(eat ? len : len - 1) * sizeof(cell_t));
    body[0].x = (i8)nx;
    body[0].y = (i8)ny;

    if (eat) {
        len++;
        score += 10;
        if (score > best)
            best = score;
        step_ticks = STEP_START - (len - 3) / 3;
        if (step_ticks < STEP_MIN)
            step_ticks = STEP_MIN;
        place_food();
    }
}

int snake_key(i32 key)
{
    int d = -1;
    switch (key) {
    case KEY_UP:    case 'w': case 'W': d = 0; break;
    case KEY_RIGHT: case 'd': case 'D': d = 1; break;
    case KEY_DOWN:  case 's': case 'S': d = 2; break;
    case KEY_LEFT:  case 'a': case 'A': d = 3; break;
    default: break;
    }

    if (d >= 0) {
        if (state == ST_OVER)
            return 0;
        if (state == ST_READY || state == ST_PAUSED)
            start();
        int last = qn ? queue[qn - 1] : dir;
        if (d != last && d != ((last + 2) & 3) && qn < 2)
            queue[qn++] = d;
        return 1;
    }

    if (key == 'r' || key == 'R' ||
        ((key == '\n' || key == ' ') && state == ST_OVER)) {
        snake_reset();
        start();
        return 1;
    }
    if (key == ' ') {
        if (state == ST_PLAY)
            state = ST_PAUSED;
        else
            start();
        return 1;
    }
    if (key == '\n' && (state == ST_READY || state == ST_PAUSED)) {
        start();
        return 1;
    }
    return 0;
}

int snake_click(void)
{
    if (state == ST_OVER) {
        snake_reset();
        start();
    } else if (state == ST_PLAY) {
        return 0;
    } else {
        start();
    }
    return 1;
}

int snake_tick(u32 ticks, int active)
{
    now = ticks;
    if (state != ST_PLAY)
        return 0;
    if (!active) {
        state = ST_PAUSED;
        return 1;
    }
    if ((i32)(ticks - last_step) < step_ticks)
        return 0;
    last_step = ticks;
    step();
    return 1;
}

/* ---------- отрисовка ---------- */

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

static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}

static void centered(i32 cx, i32 y, const char *s, u32 color, int style)
{
    font_draw(cx - font_width(s, style) / 2, y, s, color, style);
}

static void draw_eyes(i32 px, i32 py)
{
    i32 cx = px + CELL / 2, cy = py + CELL / 2;
    i32 ex[2], ey[2], ox = dxs[dir], oy = dys[dir];

    if (dir == 0)      { ex[0] = cx - 4; ey[0] = cy - 3; ex[1] = cx + 4; ey[1] = cy - 3; }
    else if (dir == 2) { ex[0] = cx - 4; ey[0] = cy + 3; ex[1] = cx + 4; ey[1] = cy + 3; }
    else if (dir == 1) { ex[0] = cx + 3; ey[0] = cy - 4; ex[1] = cx + 3; ey[1] = cy + 4; }
    else               { ex[0] = cx - 3; ey[0] = cy - 4; ex[1] = cx - 3; ey[1] = cy + 4; }

    for (int i = 0; i < 2; i++) {
        fb_fill_circle(ex[i], ey[i], 3, RGB(255, 255, 255));
        fb_fill_circle(ex[i] + ox, ey[i] + oy, 1, RGB(20, 20, 30));
    }
}

void snake_draw(i32 x, i32 y, i32 w, i32 h)
{
    const theme_t *t = theme();
    char buf[40];
    (void)h;

    i32 bx = x + (w - BOARD_W) / 2, by = y + 40;

    /* счёт */
    put_num(put_str(buf, "Score  "), score);
    font_draw(bx, y + 10, buf, t->text, FONT_BOLD);
    put_num(put_str(buf, "Best  "), best);
    font_draw(bx + BOARD_W - font_width(buf, FONT_BOLD), y + 10, buf, t->sub, FONT_BOLD);

    /* поле в шахматную клетку */
    fb_fill_rect(bx - 2, by - 2, BOARD_W + 4, BOARD_H + 4, RGB(70, 100, 50));
    for (int cy = 0; cy < SNAKE_H; cy++)
        for (int cx = 0; cx < SNAKE_W; cx++)
            fb_fill_rect(bx + cx * CELL, by + cy * CELL, CELL, CELL,
                         ((cx + cy) & 1) ? RGB(162, 209, 73) : RGB(170, 215, 81));

    /* яблоко */
    i32 fx = bx + food.x * CELL + CELL / 2, fy = by + food.y * CELL + CELL / 2 + 1;
    fb_fill_circle(fx, fy, 6, RGB(231, 71, 29));
    fb_fill_circle(fx - 2, fy - 2, 2, RGB(244, 120, 90));
    fb_fill_rect(fx, fy - 9, 2, 4, RGB(90, 60, 30));
    fb_fill_round_rect(fx + 2, fy - 9, 5, 3, 1, RGB(60, 150, 50), 255);

    /* змея: от хвоста к голове, между клетками - перемычки */
    for (int i = len - 1; i >= 0; i--) {
        i32 px = bx + body[i].x * CELL, py = by + body[i].y * CELL;
        u32 col = (i & 1) ? RGB(62, 104, 226) : RGB(70, 115, 236);
        if (i == 0)
            col = RGB(50, 92, 214);
        fb_fill_round_rect(px + 1, py + 1, CELL - 2, CELL - 2, 5, col, 255);
        if (i > 0) {
            i32 qx = bx + body[i - 1].x * CELL, qy = by + body[i - 1].y * CELL;
            if (qx != px)
                fb_fill_rect((qx < px ? qx : px) + CELL - 1, py + 3, 2, CELL - 6, col);
            else
                fb_fill_rect(px + 3, (qy < py ? qy : py) + CELL - 1, CELL - 6, 2, col);
        }
    }
    draw_eyes(bx + body[0].x * CELL, by + body[0].y * CELL);

    /* затемнение и подпись, когда игра не идёт */
    if (state != ST_PLAY) {
        const char *title, *hint;
        i32 mx = bx + BOARD_W / 2, my = by + BOARD_H / 2;
        fb_blend_rect(bx, by, BOARD_W, BOARD_H, RGB(0, 0, 0), 140);

        if (state == ST_READY) {
            title = "Snake";
            hint = "Press an arrow key to start";
        } else if (state == ST_PAUSED) {
            title = "Paused";
            hint = "Press Space to continue";
        } else {
            title = won ? "You win!" : "Game over";
            put_num(put_str(buf, "Score  "), score);
            hint = buf;
        }
        centered(mx, my - 34, title, RGB(255, 255, 255), FONT_TITLE);
        centered(mx, my + 8, hint, RGB(235, 235, 240), FONT_REGULAR);
        if (state == ST_OVER)
            centered(mx, my + 28, "Press Enter or click to play again", RGB(235, 235, 240), FONT_REGULAR);
    }

    centered(bx + BOARD_W / 2, by + BOARD_H + 10,
             "Arrows or WASD: move   Space: pause   R: restart", t->sub, FONT_REGULAR);
}
