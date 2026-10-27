/* Пункт управления EndixOS: плитки Wi-Fi/Bluetooth/AirDrop, Не беспокоить, тёмный режим,
 * акцент, ползунки яркости и звука. Яркость реально затемняет экран (wm_render),
 * Wi-Fi/Bluetooth/AirDrop/звук пока только состояния: драйверов для них нет. */
#include "control.h"
#include "fb.h"
#include "font.h"
#include "theme.h"

#define PW   316
#define PH   296
#define PADX 12
#define PY   30

static int open_;
static int wifi = 1, bt = 1, airdrop, dnd;
static int bright = 100, vol = 55;
static int drag;                        /* 0 нет, 1 яркость, 2 звук */

static i32 px(i32 W) { return W - PW - 8; }

/* Геометрия ползунков (относительно панели). */
#define SL_X  (PADX + 12)
#define SL_W  (PW - 2 * PADX - 24)
#define SL1_Y (PADX + 140 + 43)
#define SL2_Y (PADX + 210 + 43)

void cc_toggle(void) { open_ = !open_; drag = 0; }
void cc_close(void)  { open_ = 0; drag = 0; }
int  cc_is_open(void) { return open_; }
int  cc_brightness(void) { return bright; }
int  cc_volume(void) { return vol; }
int  cc_dragging(void) { return drag != 0; }

/* Значок Control Center как в macOS: два контурных "тумблера", в верхнем ручка слева, в нижнем справа.
 * Рисуется со сглаживанием 4x4; координаты внутри - в четвертях пикселя. */
static int in_capsule(i32 qx, i32 qy, i32 x0, i32 y0, i32 w, i32 h)          /* все в четвертях */
{
    i32 r = h / 2, cy = y0 + r;
    i32 lx = x0 + r, rx = x0 + w - r;
    i32 cx = qx < lx ? lx : qx > rx ? rx : qx;
    i32 dx = qx - cx, dy = qy - cy;
    return dx * dx + dy * dy <= r * r;
}

void cc_draw_icon(i32 x, u32 ink)
{
    static const i32 top[2] = { 2, 11 };                 /* верх каждого тумблера (пиксели) */
    for (i32 py = 0; py < 18; py++)
        for (i32 px = 0; px < 19; px++) {
            u32 hit = 0;
            for (i32 sy = 0; sy < 4; sy++)
                for (i32 sx = 0; sx < 4; sx++) {
                    i32 qx = px * 4 + sx * 1 + 0, qy = py * 4 + sy;
                    for (int r = 0; r < 2; r++) {
                        i32 y0 = top[r] * 4, h = 28, w = 68, x0 = 2 * 4;
                        i32 outer = in_capsule(qx, qy, x0, y0, w, h);
                        i32 inner = in_capsule(qx, qy, x0 + 5, y0 + 5, w - 10, h - 10);
                        i32 kx = r ? x0 + w - 14 : x0 + 14, ky = y0 + 14, dx = qx - kx, dy = qy - ky;
                        i32 knob = dx * dx + dy * dy <= 8 * 8;
                        if ((outer && !inner) || knob) { hit++; break; }
                    }
                }
            if (hit)
                fb_blend_pixel(x + px - 1, 1 + py, ink, hit * 255 / 16);
        }
}

/* Лупа для строки меню и Spotlight: кольцо и ручка, сглаживание 4x4 (координаты в четвертях пикселя). */
void cc_draw_search_icon(i32 x, i32 y, u32 ink)
{
    for (i32 py = 0; py < 16; py++)
        for (i32 px = 0; px < 16; px++) {
            u32 hit = 0;
            for (i32 sy = 0; sy < 4; sy++)
                for (i32 sx = 0; sx < 4; sx++) {
                    i32 qx = px * 4 + sx, qy = py * 4 + sy;
                    i32 dx = qx - 26, dy = qy - 26, d2 = dx * dx + dy * dy;
                    int ring = d2 <= 21 * 21 && d2 >= 15 * 15;
                    i32 ux = qx - 38, uy = qy - 38;                       /* ручка: отрезок по диагонали */
                    i32 t = (ux + uy) / 2;
                    int handle = 0;
                    if (t >= 0 && t <= 14) {
                        i32 ex = ux - t, ey = uy - t;
                        handle = ex * ex + ey * ey <= 6 * 6;
                    } else if (t < 0 && ux * ux + uy * uy <= 36) handle = 1;
                    else if (t > 14 && (ux - 14) * (ux - 14) + (uy - 14) * (uy - 14) <= 36) handle = 1;
                    if (ring || handle) hit++;
                }
            if (hit)
                fb_blend_pixel(x + px, y + py, ink, hit * 255 / 16);
        }
}

int cc_hit(i32 W, i32 x, i32 y)
{
    return open_ && x >= px(W) && x < px(W) + PW && y >= PY && y < PY + PH;
}

static void set_slider(int which, i32 W, i32 x)
{
    i32 v = (x - (px(W) + SL_X)) * 100 / SL_W;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (which == 1) bright = v < 20 ? 20 : v;
    else vol = v;
}

static int in(i32 x, i32 y, i32 rx, i32 ry, i32 w, i32 h)
{
    return x >= rx && y >= ry && x < rx + w && y < ry + h;
}

void cc_press(i32 W, i32 x, i32 y)
{
    i32 ox = px(W) + PADX, oy = PY + PADX;
    if (in(x, y, ox, oy, 142, 44))              wifi = !wifi;
    else if (in(x, y, ox, oy + 44, 142, 44))    bt = !bt;
    else if (in(x, y, ox, oy + 88, 142, 44))    airdrop = !airdrop;
    else if (in(x, y, ox + 150, oy, 142, 62))   dnd = !dnd;
    else if (in(x, y, ox + 150, oy + 70, 67, 62))
        theme_set_dark(!theme_is_dark());
    else if (in(x, y, ox + 225, oy + 70, 67, 62))
        theme_set_accent((theme_accent_index() + 1) % ACCENT_COUNT);
    else if (in(x, y, px(W) + SL_X - 12, PY + SL1_Y - 14, SL_W + 24, 28)) { drag = 1; set_slider(1, W, x); }
    else if (in(x, y, px(W) + SL_X - 12, PY + SL2_Y - 14, SL_W + 24, 28)) { drag = 2; set_slider(2, W, x); }
}

void cc_drag(i32 W, i32 x) { if (drag) set_slider(drag, W, x); }
void cc_release(void)      { drag = 0; }

/* ---------- рисование ---------- */

static u32 tile_col(void) { return theme_is_dark() ? RGB(88, 88, 98) : RGB(255, 255, 255); }

static void tile(i32 x, i32 y, i32 w, i32 h)
{
    fb_glass(x, y, w, h, 16, GLASS_ALL, 0, tile_col(), theme_is_dark() ? 105 : 150, 26);
}

static void icon_circle(i32 cx, i32 cy, int on, u32 on_col)
{
    fb_fill_circle(cx, cy, 15, on ? on_col : (theme_is_dark() ? RGB(110, 110, 120) : RGB(190, 190, 196)));
}

static void glyph_wifi(i32 cx, i32 cy)
{
    for (int i = 0; i < 3; i++)
        fb_fill_round_rect(cx - 8 + i * 6, cy + 5 - (i + 1) * 4 - 1, 4, (i + 1) * 4 + 1, 1, RGB(255, 255, 255), 255);
}

static void glyph_airdrop(i32 cx, i32 cy)
{
    fb_fill_circle(cx, cy, 8, RGB(255, 255, 255));
    fb_fill_circle(cx, cy, 6, RGB(120, 120, 130));
    fb_fill_circle(cx, cy, 3, RGB(255, 255, 255));
}

static void glyph_moon(i32 cx, i32 cy, u32 bg)
{
    fb_fill_circle(cx, cy, 9, RGB(255, 255, 255));
    fb_fill_circle(cx + 5, cy - 4, 8, bg);
}

static void conn_row(i32 x, i32 y, const char *name, int on, int kind)
{
    const theme_t *t = theme();
    u32 acc = kind == 1 ? RGB(10, 132, 255) : theme_accent();
    icon_circle(x + 24, y + 22, on, acc);
    if (kind == 0)      glyph_wifi(x + 24, y + 22);
    else if (kind == 1) font_draw(x + 24 - 4, y + 13, "B", RGB(255, 255, 255), FONT_BOLD);
    else                glyph_airdrop(x + 24, y + 22);
    font_draw(x + 48, y + 6, name, t->text, FONT_BOLD);
    font_draw(x + 48, y + 23, on ? "On" : "Off", t->sub, FONT_REGULAR);
}

static void slider(i32 x, i32 y, int v, int lo, int is_sun)
{
    const theme_t *t = theme();
    (void)lo;
    fb_fill_round_rect(x, y - 11, SL_W, 22, 11, theme_is_dark() ? RGB(60, 60, 68) : RGB(206, 206, 212), 255);
    i32 fx = (i32)((u32)SL_W * (u32)v / 100);
    if (fx < 22) fx = 22;
    fb_fill_round_rect(x, y - 11, fx, 22, 11, RGB(255, 255, 255), 255);
    u32 gc = RGB(130, 130, 138);
    if (is_sun) {                                       /* солнце */
        fb_fill_circle(x + 11, y, 4, gc);
        for (int i = 0; i < 8; i++) {
            static const i8 d[8][2] = {{0,-7},{5,-5},{7,0},{5,5},{0,7},{-5,5},{-7,0},{-5,-5}};
            fb_fill_rect(x + 11 + d[i][0], y + d[i][1], 1, 1, gc);
        }
    } else {                                            /* динамик */
        fb_fill_rect(x + 7, y - 2, 4, 5, gc);
        fb_fill_rect(x + 11, y - 4, 2, 9, gc);
        fb_fill_rect(x + 14, y - 2, 1, 5, gc);
    }
    (void)t;
}

void cc_bounds(i32 W, i32 *x, i32 *y, i32 *w, i32 *h)   /* панель вместе с тенью */
{
    *x = px(W) - 10; *y = PY - 4; *w = PW + 20; *h = PH + 20;
}

void cc_draw(i32 W)
{
    if (open_)
        cc_draw_any(W);
}

void cc_draw_any(i32 W)                                 /* рисует панель, даже если она уже закрыта (для исчезновения) */
{
    const theme_t *t = theme();
    i32 x = px(W), y = PY;
    i32 ox = x + PADX, oy = y + PADX;

    for (int k = 0; k < 6; k++)                         /* мягкая тень */
        fb_fill_round_rect(x - k, y - k + 5, PW + 2 * k, PH + 2 * k, 20 + k, RGB(0, 0, 0), 9);
    fb_glass(x, y, PW, PH, 24, GLASS_ALL, 12, theme_is_dark() ? RGB(36, 36, 42) : RGB(238, 238, 243),
             theme_is_dark() ? 165 : 165, 40);

    tile(ox, oy, 142, 132);
    conn_row(ox + 6, oy + 0, "Wi-Fi", wifi, 0);
    conn_row(ox + 6, oy + 44, "Bluetooth", bt, 1);
    conn_row(ox + 6, oy + 88, "AirDrop", airdrop, 2);

    tile(ox + 150, oy, 142, 62);                        /* Не беспокоить */
    icon_circle(ox + 150 + 28, oy + 31, dnd, RGB(94, 92, 230));
    glyph_moon(ox + 150 + 28, oy + 31, dnd ? RGB(94, 92, 230) : (theme_is_dark() ? RGB(110, 110, 120) : RGB(190, 190, 196)));
    font_draw(ox + 150 + 52, oy + 11, "Focus", t->text, FONT_BOLD);
    font_draw(ox + 150 + 52, oy + 30, dnd ? "Do Not Disturb" : "Off", t->sub, FONT_REGULAR);

    tile(ox + 150, oy + 70, 67, 62);                    /* тёмный режим */
    icon_circle(ox + 150 + 33, oy + 70 + 22, theme_is_dark(), RGB(10, 132, 255));
    glyph_moon(ox + 150 + 33, oy + 70 + 22, theme_is_dark() ? RGB(10, 132, 255) : RGB(190, 190, 196));
    font_draw(ox + 150 + 33 - font_width("Dark", FONT_REGULAR) / 2, oy + 70 + 41, "Dark", t->text, FONT_REGULAR);

    tile(ox + 225, oy + 70, 67, 62);                    /* акцент */
    fb_fill_circle(ox + 225 + 33, oy + 70 + 22, 15, theme_accent());
    fb_fill_circle(ox + 225 + 33, oy + 70 + 22, 6, RGB(255, 255, 255));
    font_draw(ox + 225 + 33 - font_width("Accent", FONT_REGULAR) / 2, oy + 70 + 41, "Accent", t->text, FONT_REGULAR);

    tile(ox, oy + 140, 292, 62);
    font_draw(ox + 12, oy + 140 + 7, "Display", t->text, FONT_BOLD);
    slider(x + SL_X, y + SL1_Y, (bright - 20) * 100 / 80, 20, 1);

    tile(ox, oy + 210, 292, 62);
    font_draw(ox + 12, oy + 210 + 7, "Sound", t->text, FONT_BOLD);
    slider(x + SL_X, y + SL2_Y, vol, 0, 0);
}
