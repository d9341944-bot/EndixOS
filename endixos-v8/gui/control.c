/* Пункт управления EndixOS в стиле macOS Tahoe: сетка плиток на "жидком стекле" и редактор.
 * Плитки (Wi-Fi, Bluetooth, AirDrop, Focus, Dark Mode, ползунки, быстрые кнопки) укладываются в сетку 4 колонки
 * по порядку списка. "Edit Controls": убрать плитку (минус), перетащить на другое место, добавить из списка.
 * Яркость затемняет экран; тёмный режим, акцент и Reduce Transparency работают по-настоящему;
 * Wi-Fi, Bluetooth, AirDrop, Focus и Now Playing пока только состояния: драйверов для них нет. */
#include "control.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "wm.h"

#define CELL 66
#define GAP  8
#define PITCH (CELL + GAP)
#define PW   316
#define PADX 14
#define PY   30
#define COLS 4
#define MAXROWS 12
#define FOOT 32

enum { C_WIFI, C_FOCUS, C_BT, C_AIRDROP, C_DARK, C_ACCENT, C_MISSION, C_SPOTLIGHT, C_SETTINGS, C_DISPLAY, C_SOUND,
       C_NOWPLAYING, C_TERMINAL, C_FILES, C_LOCK, C_TRANSP, C_N };
enum { K_TOGGLE, K_SLIDER, K_ACTION, K_MEDIA };
typedef struct { const char *name; u8 w, h, kind; } ctl_t;
static const ctl_t CT[C_N] = {
    { "Wi-Fi", 2, 1, K_TOGGLE }, { "Focus", 2, 1, K_TOGGLE }, { "Bluetooth", 1, 1, K_TOGGLE }, { "AirDrop", 1, 1, K_TOGGLE },
    { "Dark Mode", 2, 1, K_TOGGLE }, { "Accent Color", 1, 1, K_ACTION }, { "Mission Control", 1, 1, K_ACTION }, { "Spotlight", 1, 1, K_ACTION },
    { "Settings", 1, 1, K_ACTION }, { "Display", 4, 1, K_SLIDER }, { "Sound", 4, 1, K_SLIDER },
    { "Now Playing", 2, 2, K_MEDIA }, { "Terminal", 1, 1, K_ACTION }, { "Files", 1, 1, K_ACTION }, { "Lock Screen", 1, 1, K_ACTION },
    { "Transparency", 1, 1, K_TOGGLE },
};
static const u8 DEF_ORDER[] = { C_WIFI, C_FOCUS, C_BT, C_AIRDROP, C_DARK, C_ACCENT, C_MISSION, C_SPOTLIGHT, C_SETTINGS, C_DISPLAY, C_SOUND };

static int open_;
static int wifi = 1, bt = 1, airdrop, dnd;
static int bright = 100, vol = 55;
static u8  order[C_N], norder;
static i32 cx_[C_N], cy_[C_N];                 /* ячейка (колонка, строка) плитки */
static i32 dx_[C_N], dy_[C_N];                 /* где нарисована, 1/16 пикселя от начала сетки */
static int mode;                               /* 0 обычный, 1 редактирование, 2 галерея "Add a Control" */
static int sdrag = -1;                         /* ползунок, который тянут */
static i32 bar_x, bar_w;
static int rdrag = -1;                         /* плитка, которую переставляют */
static i32 rgx, rgy;                           /* куда тянут (центр, экранные координаты) */
static i32 grab_x, grab_y;
static i32 ph_v;                               /* текущая высота панели, 1/16 пикселя */
static int inited;

static i32 px(i32 W) { return W - PW - 8; }
static int in(i32 x, i32 y, i32 rx, i32 ry, i32 w, i32 h) { return x >= rx && y >= ry && x < rx + w && y < ry + h; }

static int enabled(int id) { for (int i = 0; i < norder; i++) if (order[i] == id) return 1; return 0; }

static void relayout(void)
{
    u8 occ[MAXROWS][COLS];
    for (int r = 0; r < MAXROWS; r++) for (int c = 0; c < COLS; c++) occ[r][c] = 0;
    for (int i = 0; i < norder; i++) {
        int id = order[i], w = CT[id].w, h = CT[id].h, done = 0;
        for (int r = 0; r + h <= MAXROWS && !done; r++)
            for (int c = 0; c + w <= COLS && !done; c++) {
                int free = 1;
                for (int j = 0; j < h && free; j++) for (int k = 0; k < w; k++) if (occ[r + j][c + k]) { free = 0; break; }
                if (!free) continue;
                for (int j = 0; j < h; j++) for (int k = 0; k < w; k++) occ[r + j][c + k] = 1;
                cx_[id] = c; cy_[id] = r; done = 1;
            }
        if (!done) { cx_[id] = 0; cy_[id] = MAXROWS; }          /* не поместилась: за пределами сетки */
    }
}

static void snap_all(void) { for (int i = 0; i < norder; i++) { dx_[order[i]] = cx_[order[i]] * PITCH << 4; dy_[order[i]] = cy_[order[i]] * PITCH << 4; } }

static void init_once(void)
{
    if (inited) return;
    inited = 1;
    norder = 0;
    for (unsigned i = 0; i < sizeof DEF_ORDER; i++) order[norder++] = DEF_ORDER[i];
    relayout(); snap_all();
}

static int grid_rows(void)
{
    int rows = 0;
    for (int i = 0; i < norder; i++) { int id = order[i]; if (cy_[id] < MAXROWS) { int b = cy_[id] + CT[id].h; if (b > rows) rows = b; } }
    return rows;
}
static int navail(void) { int n = 0; for (int id = 0; id < C_N; id++) if (!enabled(id)) n++; return n; }
static i32 target_h(void)
{
    init_once();
    if (mode == 2) { int n = navail(), rows = n ? (n + 1) / 2 : 1; return PADX * 2 + 34 + 8 + rows * 52 - 8; }
    int rows = grid_rows();
    i32 gh = rows ? rows * PITCH - GAP : CELL;
    return PADX * 2 + gh + 8 + FOOT;
}
static i32 cur_h(void) { init_once(); if (ph_v == 0) ph_v = target_h() << 4; return ph_v >> 4; }

void cc_toggle(void) { open_ = !open_; sdrag = rdrag = -1; if (!open_) mode = 0; }
void cc_close(void)  { open_ = 0; sdrag = rdrag = -1; mode = 0; }
int  cc_is_open(void) { return open_; }
int  cc_brightness(void) { return bright; }
int  cc_volume(void) { return vol; }
void cc_set_brightness(int v) { bright = v < 20 ? 20 : v > 100 ? 100 : v; }
void cc_set_volume(int v) { vol = v < 0 ? 0 : v > 100 ? 100 : v; }
int  cc_dragging(void) { return sdrag >= 0 || rdrag >= 0; }
int  cc_editing(void) { return mode != 0; }
int  cc_escape(void)                        /* Esc: сначала выйти из редактора */
{
    if (mode == 2) { mode = 1; return 1; }
    if (mode == 1) { mode = 0; return 1; }
    return 0;
}

void cc_get_layout(u8 out[16])              /* что включено и в каком порядке; id+1, 0 - конец; 0xFF - пусто */
{
    init_once();
    for (int i = 0; i < 16; i++) out[i] = 0;
    if (!norder) { out[0] = 0xFF; return; }
    for (int i = 0; i < norder && i < 16; i++) out[i] = (u8)(order[i] + 1);
}
void cc_set_layout(const u8 in[16])
{
    init_once();
    if (in[0] == 0) return;                                    /* не сохранено: оставляем по умолчанию */
    norder = 0;
    if (in[0] != 0xFF)
        for (int i = 0; i < 16 && in[i]; i++)
            if (in[i] >= 1 && in[i] <= C_N && !enabled(in[i] - 1)) order[norder++] = (u8)(in[i] - 1);
    relayout(); snap_all();
    ph_v = target_h() << 4;
}

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


/* ---------- геометрия ---------- */

static void rect_of(int id, i32 W, i32 *x, i32 *y, i32 *w, i32 *h)       /* по целевой ячейке */
{
    *x = px(W) + PADX + cx_[id] * PITCH; *y = PY + PADX + cy_[id] * PITCH;
    *w = CT[id].w * PITCH - GAP; *h = CT[id].h * PITCH - GAP;
}
static void drect_of(int id, i32 W, i32 *x, i32 *y, i32 *w, i32 *h)      /* где нарисована сейчас */
{
    *x = px(W) + PADX + (dx_[id] >> 4); *y = PY + PADX + (dy_[id] >> 4);
    *w = CT[id].w * PITCH - GAP; *h = CT[id].h * PITCH - GAP;
    if (id == rdrag) { *x = rgx - grab_x; *y = rgy - grab_y; }
}
static i32 foot_y(void) { return PY + cur_h() - PADX - FOOT + 2; }

int cc_hit(i32 W, i32 x, i32 y) { return open_ && x >= px(W) && x < px(W) + PW && y >= PY && y < PY + cur_h(); }

static void set_slider(int id, i32 x)
{
    i32 v = (x - bar_x) * 100 / (bar_w > 0 ? bar_w : 1);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (id == C_DISPLAY) bright = v < 20 ? 20 : v;
    else vol = v;
}

static int tile_at(i32 W, i32 x, i32 y)
{
    for (int i = norder - 1; i >= 0; i--) {
        i32 rx, ry, rw, rh; rect_of(order[i], W, &rx, &ry, &rw, &rh);
        if (in(x, y, rx, ry, rw, rh)) return order[i];
    }
    return -1;
}

static void remove_id(int id)
{
    int k = 0;
    for (int i = 0; i < norder; i++) if (order[i] != id) order[k++] = order[i];
    norder = (u8)k;
    relayout();
}

int cc_press(i32 W, i32 x, i32 y)
{
    init_once();
    i32 fy = foot_y();
    if (mode == 2) {                                           /* галерея */
        i32 gx = px(W) + PADX, gy = PY + PADX;
        if (in(x, y, gx, gy, 90, 30)) { mode = 1; return CCA_NONE; }      /* назад */
        int k = 0;
        for (int id = 0; id < C_N; id++) {
            if (enabled(id)) continue;
            i32 rx = gx + (k % 2) * 148, ry = gy + 42 + (k / 2) * 52;
            if (in(x, y, rx, ry, 140, 44)) {
                order[norder++] = (u8)id; relayout();
                dx_[id] = cx_[id] * PITCH << 4; dy_[id] = cy_[id] * PITCH << 4;
                mode = 1;
                return CCA_NONE;
            }
            k++;
        }
        return CCA_NONE;
    }
    if (mode == 1) {                                           /* редактор */
        if (in(x, y, px(W) + PADX, fy, 150, 30) && navail()) { mode = 2; return CCA_NONE; }
        if (in(x, y, px(W) + PW - PADX - 90, fy, 90, 30)) { mode = 0; return CCA_NONE; }
        int id = tile_at(W, x, y);
        if (id >= 0) {
            i32 rx, ry, rw, rh; rect_of(id, W, &rx, &ry, &rw, &rh);
            if ((x - (rx + 4)) * (x - (rx + 4)) + (y - (ry + 4)) * (y - (ry + 4)) <= 12 * 12) { remove_id(id); return CCA_NONE; }   /* минус */
            drect_of(id, W, &rx, &ry, &rw, &rh);
            rdrag = id; grab_x = x - rx; grab_y = y - ry; rgx = x; rgy = y;
        }
        return CCA_NONE;
    }
    if (in(x, y, px(W) + (PW - 130) / 2, fy, 130, 30)) { mode = 1; return CCA_NONE; }        /* Edit Controls */
    int id = tile_at(W, x, y);
    if (id < 0) return CCA_NONE;
    i32 rx, ry, rw, rh; rect_of(id, W, &rx, &ry, &rw, &rh);
    switch (id) {
    case C_WIFI: wifi = !wifi; break;
    case C_BT: bt = !bt; break;
    case C_AIRDROP: airdrop = !airdrop; break;
    case C_FOCUS: dnd = !dnd; break;
    case C_DARK: theme_set_dark(!theme_is_dark()); break;
    case C_TRANSP: wm_set_option(WM_OPT_REDUCE_TRANSP, !wm_get_option(WM_OPT_REDUCE_TRANSP)); break;
    case C_ACCENT: theme_set_accent((theme_accent_index() + 1) % ACCENT_COUNT); break;
    case C_DISPLAY: case C_SOUND: sdrag = id; bar_x = rx + 14; bar_w = rw - 28; set_slider(id, x); break;
    case C_MISSION: return CCA_MISSION;
    case C_SPOTLIGHT: return CCA_SPOTLIGHT;
    case C_SETTINGS: return CCA_SETTINGS;
    case C_TERMINAL: return CCA_TERMINAL;
    case C_FILES: return CCA_FILES;
    case C_LOCK: return CCA_LOCK;
    default: break;
    }
    return CCA_NONE;
}

void cc_drag(i32 W, i32 x, i32 y)
{
    if (sdrag >= 0) { set_slider(sdrag, x); return; }
    if (rdrag < 0) return;
    rgx = x; rgy = y;
    for (int i = 0; i < norder; i++) {                         /* над другой плиткой: меняемся местами в списке */
        int id = order[i];
        if (id == rdrag) continue;
        i32 rx, ry, rw, rh; rect_of(id, W, &rx, &ry, &rw, &rh);
        if (in(x, y, rx, ry, rw, rh)) {
            int from = 0;
            while (order[from] != rdrag) from++;
            u8 m = (u8)rdrag;
            if (from < i) for (int k = from; k < i; k++) order[k] = order[k + 1];
            else for (int k = from; k > i; k--) order[k] = order[k - 1];
            order[i] = m;
            relayout();
            break;
        }
    }
}
void cc_release(void) { sdrag = -1; rdrag = -1; }

int cc_tick(u32 dt)
{
    init_once();
    int moving = 0;
    for (int i = 0; i < norder; i++) {
        int id = order[i];
        i32 wx = cx_[id] * PITCH << 4, wy = cy_[id] * PITCH << 4;
        for (u32 k = 0; k < (dt > 12 ? 12 : dt) && (dx_[id] != wx || dy_[id] != wy); k++) {
            i32 d = wy - dy_[id], st = d * 3 / 10;
            if (st == 0 && d) st = d > 0 ? 1 : -1;
            dy_[id] += st;
            d = wx - dx_[id]; st = d * 3 / 10;
            if (st == 0 && d) st = d > 0 ? 1 : -1;
            dx_[id] += st;
        }
        if (dx_[id] != wx || dy_[id] != wy) moving = 1;
    }
    i32 want = target_h() << 4;
    if (ph_v == 0) ph_v = want;
    for (u32 k = 0; k < (dt > 12 ? 12 : dt) && ph_v != want; k++) {
        i32 d = want - ph_v, st = d * 3 / 10;
        if (st == 0) st = d > 0 ? 1 : -1;
        ph_v += st;
    }
    if (ph_v != want) moving = 1;
    if (rdrag >= 0) moving = 1;
    return moving;
}

/* ---------- рисование ---------- */

static u32 tile_col(void) { return theme_is_dark() ? RGB(96, 96, 108) : RGB(255, 255, 255); }
static u32 disc_off(void) { return theme_is_dark() ? RGB(120, 120, 130) : RGB(176, 176, 184); }

static int isq(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }

static void glyph(int id, i32 cx, i32 cy, u32 c, u32 bg)
{
    switch (id) {
    case C_WIFI:
        for (int i = 0; i < 3; i++) fb_fill_round_rect(cx - 8 + i * 6, cy + 6 - (i + 1) * 4 - 1, 4, (i + 1) * 4 + 1, 1, c, 255);
        break;
    case C_BT:
        font_draw(cx - font_width("B", FONT_BOLD) / 2, cy - 9, "B", c, FONT_BOLD);
        break;
    case C_AIRDROP:
        fb_fill_circle(cx, cy, 9, c); fb_fill_circle(cx, cy, 7, bg); fb_fill_circle(cx, cy, 4, c); fb_fill_circle(cx, cy, 2, bg); fb_fill_circle(cx, cy, 1, c);
        break;
    case C_FOCUS:
        fb_fill_circle(cx, cy, 9, c); fb_fill_circle(cx + 5, cy - 4, 8, bg);
        break;
    case C_DARK:
        fb_fill_circle(cx, cy, 10, c); fb_fill_circle(cx, cy, 8, bg);
        for (int dy = -8; dy <= 8; dy++) { int hw = isq(64 - dy * dy); fb_fill_rect(cx - hw, cy + dy, hw, 1, c); }
        break;
    case C_ACCENT:
        fb_fill_circle(cx, cy, 11, theme_accent()); fb_fill_circle(cx, cy, 4, RGB(255, 255, 255));
        break;
    case C_MISSION:
        fb_fill_round_rect(cx - 10, cy - 8, 14, 11, 2, c, 255); fb_fill_round_rect(cx - 8, cy - 6, 10, 7, 1, bg, 255);
        fb_fill_round_rect(cx - 4, cy - 2, 14, 11, 2, c, 255); fb_fill_round_rect(cx - 2, cy, 10, 7, 1, bg, 255);
        break;
    case C_SPOTLIGHT: cc_draw_search_icon(cx - 8, cy - 8, c); break;
    case C_SETTINGS:
        fb_fill_circle(cx, cy, 8, c);
        fb_fill_rect(cx - 2, cy - 11, 4, 4, c); fb_fill_rect(cx - 2, cy + 8, 4, 4, c); fb_fill_rect(cx - 11, cy - 2, 4, 4, c); fb_fill_rect(cx + 8, cy - 2, 4, 4, c);
        fb_fill_circle(cx, cy, 4, bg);
        break;
    case C_TERMINAL: font_draw(cx - font_width(">_", FONT_BOLD) / 2, cy - 9, ">_", c, FONT_BOLD); break;
    case C_FILES:
        fb_fill_round_rect(cx - 10, cy - 7, 8, 5, 2, c, 255); fb_fill_round_rect(cx - 10, cy - 4, 20, 13, 3, c, 255);
        break;
    case C_LOCK:
        fb_fill_circle(cx, cy - 4, 6, c); fb_fill_circle(cx, cy - 4, 3, bg);
        fb_fill_rect(cx - 7, cy - 3, 14, 4, bg); fb_fill_round_rect(cx - 8, cy - 1, 16, 11, 3, c, 255);
        break;
    case C_TRANSP:
        fb_fill_round_rect(cx - 10, cy - 10, 14, 14, 3, c, 255); fb_fill_round_rect(cx - 8, cy - 8, 10, 10, 2, bg, 255);
        fb_fill_round_rect(cx - 4, cy - 4, 14, 14, 3, c, 255); fb_fill_round_rect(cx - 2, cy - 2, 10, 10, 2, bg, 255);
        break;
    case C_DISPLAY:
        fb_fill_circle(cx, cy, 4, c);
        for (int i = 0; i < 8; i++) { static const i8 d[8][2] = {{0,-8},{6,-6},{8,0},{6,6},{0,8},{-6,6},{-8,0},{-6,-6}}; fb_fill_rect(cx + d[i][0], cy + d[i][1], 2, 2, c); }
        break;
    case C_SOUND:
        fb_fill_rect(cx - 6, cy - 3, 4, 7, c); fb_fill_rect(cx - 2, cy - 6, 3, 13, c); fb_fill_rect(cx + 3, cy - 3, 2, 7, c);
        break;
    default: break;
    }
}

static int is_on(int id)
{
    switch (id) {
    case C_WIFI: return wifi;
    case C_BT: return bt;
    case C_AIRDROP: return airdrop;
    case C_FOCUS: return dnd;
    case C_DARK: return theme_is_dark();
    case C_TRANSP: return wm_get_option(WM_OPT_REDUCE_TRANSP);
    default: return 0;
    }
}

static void disc(int id, i32 cx, i32 cy, i32 r, int on)
{
    u32 acc = id == C_BT ? RGB(10, 132, 255) : id == C_FOCUS ? RGB(94, 92, 230) : id == C_DARK ? RGB(10, 132, 255) : theme_accent();
    u32 bg = on ? RGB(255, 255, 255) : disc_off();
    fb_fill_circle(cx, cy, r, bg);
    glyph(id, cx, cy, on ? acc : RGB(255, 255, 255), bg);
}

static void slider_bar(i32 x, i32 y, i32 w, int v, int id)
{
    fb_fill_round_rect(x, y, w, 24, 12, theme_is_dark() ? RGB(52, 52, 60) : RGB(204, 204, 212), 255);
    i32 fx = (i32)((u32)w * (u32)v / 100);
    if (fx < 24) fx = 24;
    fb_fill_round_rect(x, y, fx, 24, 12, RGB(255, 255, 255), 255);
    glyph(id, x + 12, y + 12, RGB(120, 120, 130), RGB(255, 255, 255));
}

static void draw_tile(int id, i32 x, i32 y, i32 w, i32 h)
{
    const theme_t *t = theme();
    int on = is_on(id);
    int round = CT[id].w == 1 && CT[id].h == 1;
    fb_glass(x, y, w, h, round ? CELL / 2 : 20, GLASS_ALL, 0, tile_col(), theme_is_dark() ? 112 : 160, 26);
    switch (CT[id].kind) {
    case K_SLIDER:
        font_draw(x + 14, y + 8, CT[id].name, t->text, FONT_BOLD);
        slider_bar(x + 14, y + 32, w - 28, id == C_DISPLAY ? (bright - 20) * 100 / 80 : vol, id);
        break;
    case K_MEDIA: {
        font_draw(x + 12, y + 12, "Not Playing", t->text, FONT_BOLD);
        font_draw(x + 12, y + 32, "No audio yet", t->sub, FONT_REGULAR);
        i32 by = y + h - 36, cx = x + w / 2;
        fb_fill_rect(cx - 36, by + 2, 3, 12, t->text); for (int i = 0; i < 6; i++) fb_fill_rect(cx - 33 + i, by + 8 - i - 1, 1, 2 * i + 2, t->text);
        for (int i = 0; i < 8; i++) fb_fill_rect(cx - 5 + i, by + i / 2 - 2 + 1, 1, 14 - i - 0, t->text);
        for (int i = 0; i < 6; i++) fb_fill_rect(cx + 27 + i, by + 2 + i / 2, 1, 12 - i, t->text);
        break;
    }
    default:
        if (round) {
            i32 cx = x + w / 2, cy = y + h / 2;
            if (CT[id].kind == K_TOGGLE) disc(id, cx, cy, 22, on);
            else { fb_fill_circle(cx, cy, 22, disc_off()); glyph(id, cx, cy, RGB(255, 255, 255), disc_off()); }
        } else {
            disc(id, x + 28, y + h / 2, 17, on);
            font_draw(x + 52, y + h / 2 - 18, id == C_WIFI ? "Wi-Fi" : CT[id].name, t->text, FONT_BOLD);
            const char *sub = id == C_FOCUS ? (on ? "Do Not Disturb" : "Off") : (on ? "On" : "Off");
            font_draw(x + 52, y + h / 2 + 2, sub, t->sub, FONT_REGULAR);
        }
        break;
    }
}

static void minus_badge(i32 x, i32 y)
{
    fb_fill_circle(x + 4, y + 4, 11, RGB(0, 0, 0));
    fb_fill_circle(x + 4, y + 4, 10, RGB(222, 222, 228));
    fb_fill_rect(x - 1, y + 3, 10, 2, RGB(50, 50, 56));
}

static void pill(i32 x, i32 y, i32 w, i32 h, const char *label, int primary, int enabled)
{
    const theme_t *t = theme();
    if (primary) fb_fill_round_rect(x, y, w, h, h / 2, theme_accent(), enabled ? 255 : 120);
    else fb_glass(x, y, w, h, h / 2, GLASS_ALL, 0, tile_col(), theme_is_dark() ? 112 : 160, 20);
    u32 col = primary ? RGB(255, 255, 255) : (enabled ? t->text : t->sub);
    font_draw(x + (w - font_width(label, FONT_BOLD)) / 2, y + (h - font_height[FONT_BOLD]) / 2, label, col, FONT_BOLD);
}

void cc_bounds(i32 W, i32 *x, i32 *y, i32 *w, i32 *h)   /* панель вместе с тенью */
{
    *x = px(W) - 10; *y = PY - 4; *w = PW + 20; *h = cur_h() + 20;
}

void cc_draw(i32 W)
{
    if (open_)
        cc_draw_any(W);
}

void cc_draw_any(i32 W)                                 /* рисует панель, даже если она уже закрыта (для исчезновения) */
{
    const theme_t *t = theme();
    init_once();
    i32 x = px(W), y = PY, ph = cur_h();

    for (int k = 0; k < 6; k++)                         /* мягкая тень */
        fb_fill_round_rect(x - k, y - k + 5, PW + 2 * k, ph + 2 * k, 24 + k, RGB(0, 0, 0), 9);
    fb_glass(x, y, PW, ph, 30, GLASS_ALL, 14, theme_is_dark() ? RGB(36, 36, 42) : RGB(238, 238, 243),
             theme_is_dark() ? 118 : 112, 44);

    if (mode == 2) {                                    /* галерея: что можно добавить */
        i32 gx = x + PADX, gy = y + PADX;
        pill(gx, gy, 78, 30, "Back", 0, 1);
        font_draw(x + PW - PADX - font_width("Add a Control", FONT_BOLD), gy + 6, "Add a Control", t->text, FONT_BOLD);
        int k = 0;
        for (int id = 0; id < C_N; id++) {
            if (enabled(id)) continue;
            i32 rx = gx + (k % 2) * 148, ry = gy + 42 + (k / 2) * 52;
            fb_glass(rx, ry, 140, 44, 22, GLASS_ALL, 0, tile_col(), theme_is_dark() ? 112 : 160, 20);
            fb_fill_circle(rx + 24, ry + 22, 15, disc_off());
            glyph(id, rx + 24, ry + 22, RGB(255, 255, 255), disc_off());
            char nm[24]; int n = 0;
            while (CT[id].name[n] && n < 22) { nm[n] = CT[id].name[n]; n++; }
            nm[n] = 0;
            while (n > 1 && font_width(nm, FONT_REGULAR) > 140 - 52 - 6) nm[--n] = 0;
            font_draw(rx + 46, ry + 13, nm, t->text, FONT_REGULAR);
            k++;
        }
        if (!k) font_draw(gx + 4, gy + 54, "All controls are added.", t->sub, FONT_REGULAR);
        return;
    }

    for (int i = 0; i < norder; i++) {                  /* сначала все плитки, потом ту, что тащат: она сверху */
        int id = order[i];
        if (id == rdrag || cy_[id] >= MAXROWS) continue;
        i32 rx, ry, rw, rh; drect_of(id, W, &rx, &ry, &rw, &rh);
        draw_tile(id, rx, ry, rw, rh);
    }
    if (mode == 1) {                                    /* пустые клетки: сюда можно поставить */
        u8 occ[MAXROWS + 1][COLS];
        for (int r = 0; r <= MAXROWS; r++) for (int c = 0; c < COLS; c++) occ[r][c] = 0;
        for (int i = 0; i < norder; i++) { int id = order[i]; if (cy_[id] >= MAXROWS) continue;
            for (int j = 0; j < CT[id].h; j++) for (int k = 0; k < CT[id].w; k++) occ[cy_[id] + j][cx_[id] + k] = 1; }
        int rows = grid_rows();
        for (int r = 0; r < (rows ? rows : 1); r++) for (int c = 0; c < COLS; c++)
            if (!occ[r][c]) fb_fill_round_rect(x + PADX + c * PITCH, y + PADX + r * PITCH, CELL, CELL, CELL / 2, RGB(255, 255, 255), theme_is_dark() ? 22 : 60);
        for (int i = 0; i < norder; i++) {
            int id = order[i];
            if (id == rdrag || cy_[id] >= MAXROWS) continue;
            i32 rx, ry, rw, rh; drect_of(id, W, &rx, &ry, &rw, &rh);
            minus_badge(rx, ry);
        }
    }
    if (rdrag >= 0) {
        i32 rx, ry, rw, rh; drect_of(rdrag, W, &rx, &ry, &rw, &rh);
        { i32 rr = CT[rdrag].w == 1 && CT[rdrag].h == 1 ? CELL / 2 : 20; for (int k = 0; k < 5; k++) fb_fill_round_rect(rx - k + 1, ry - k + 8, rw + 2 * k - 2, rh + 2 * k - 2, rr + k, RGB(0, 0, 0), 14); }
        draw_tile(rdrag, rx, ry, rw, rh);
    }
    i32 fy = y + ph - PADX - FOOT + 2;                  /* нижняя строка */
    if (mode == 0) pill(x + (PW - 130) / 2, fy, 130, 30, "Edit Controls", 0, 1);
    else { pill(x + PADX, fy, 150, 30, "Add a Control", 0, navail() > 0); pill(x + PW - PADX - 90, fy, 90, 30, "Done", 1, 1); }
}
