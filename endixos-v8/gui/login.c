#include "login.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "../kernel/install.h"

extern void sys_reboot(void);
extern void sys_shutdown(void);

static int  have_user, active, fading, pw_len, wrong, blur_ok;
static char c_user[16], c_full[48], c_sh[100], pw[64];
static u32  now, last, t_shake, t_fade, t_wrong;
static i32  fade_a = 1024, mx, my, mprev;
static i32  hit_go, hit_rs, hit_sd;                       /* что под курсором: подсветка */
static u32  blur[FB_MAX_W * FB_MAX_H];
static u32  small_a[(FB_MAX_W / 4 + 2) * (FB_MAX_H / 4 + 2)], small_b[(FB_MAX_W / 4 + 2) * (FB_MAX_H / 4 + 2)];

static void scopy(char *d, const char *s, int n) { int i = 0; while (s[i] && i < n - 1) { d[i] = s[i]; i++; } d[i] = 0; }

void login_open(const char *user, const char *fullname, const char *salthash)
{
    scopy(c_user, user, 16); scopy(c_full, fullname && fullname[0] ? fullname : user, 48); scopy(c_sh, salthash, 100);
    have_user = 1;
    login_relock();
}
int login_relock(void)
{
    if (!have_user) return 0;
    active = 1; fading = 0; fade_a = 1024; pw_len = 0; pw[0] = 0; wrong = 0; blur_ok = 0; t_shake = 0; t_wrong = 0;
    return 1;
}
int login_active(void) { return active; }
int login_fading(void) { return active && fading; }
i32 login_alpha(void) { return fade_a; }
const char *login_user(void) { return have_user ? c_user : 0; }

/* ---------- размытие обоев: уменьшаем в 4 раза, размываем, растягиваем обратно ---------- */
static void box_pass(u32 *src, u32 *dst, i32 w, i32 h, int vert, int r)
{
    i32 len = vert ? h : w, lines = vert ? w : h;
    for (i32 l = 0; l < lines; l++) {
        u32 sr = 0, sg = 0, sb = 0; i32 cnt = 0;
        for (i32 i = -r; i < len + r; i++) {
            i32 add = i + r, rem = i - r - 1;                    /* окно [i-r, i+r] */
            if (add < len) { u32 p = vert ? src[add * w + l] : src[l * w + add]; sr += p >> 16 & 255; sg += p >> 8 & 255; sb += p & 255; cnt++; }
            if (rem >= 0 && rem < len) { u32 p = vert ? src[rem * w + l] : src[l * w + rem]; sr -= p >> 16 & 255; sg -= p >> 8 & 255; sb -= p & 255; cnt--; }
            if (i >= 0 && i < len && cnt) {
                u32 o = (sr / (u32)cnt) << 16 | (sg / (u32)cnt) << 8 | (sb / (u32)cnt);
                if (vert) dst[i * w + l] = o; else dst[l * w + i] = o;
            }
        }
    }
}
static void make_blur(void)
{
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    fb_restore_background();
    const u32 *bg = fb_buffer();
    i32 sw = W / 4, sh = H / 4;
    for (i32 y = 0; y < sh; y++)
        for (i32 x = 0; x < sw; x++) {
            u32 r = 0, g = 0, b = 0;
            for (i32 j = 0; j < 4; j++) for (i32 i = 0; i < 4; i++) { u32 p = bg[(y * 4 + j) * W + x * 4 + i]; r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; }
            small_a[y * sw + x] = (r / 16) << 16 | (g / 16) << 8 | (b / 16);
        }
    for (int it = 0; it < 2; it++) { box_pass(small_a, small_b, sw, sh, 0, 5); box_pass(small_b, small_a, sw, sh, 1, 5); }
    for (i32 y = 0; y < H; y++) {
        i32 fy = (y * 256 - 512) / 4; if (fy < 0) fy = 0; if (fy > (sh - 1) * 256) fy = (sh - 1) * 256;
        i32 y0 = fy >> 8, y1 = y0 + 1 < sh ? y0 + 1 : y0, wy = fy & 255;
        for (i32 x = 0; x < W; x++) {
            i32 fx = (x * 256 - 512) / 4; if (fx < 0) fx = 0; if (fx > (sw - 1) * 256) fx = (sw - 1) * 256;
            i32 x0 = fx >> 8, x1 = x0 + 1 < sw ? x0 + 1 : x0, wx = fx & 255;
            u32 o = 0;
            for (int s = 16; s >= 0; s -= 8) {
                u32 a = small_a[y0 * sw + x0] >> s & 255, b = small_a[y0 * sw + x1] >> s & 255, c = small_a[y1 * sw + x0] >> s & 255, d = small_a[y1 * sw + x1] >> s & 255;
                u32 top = a * (u32)(256 - wx) + b * (u32)wx, bot = c * (u32)(256 - wx) + d * (u32)wx;
                u32 v = (top * (u32)(256 - wy) + bot * (u32)wy) >> 16;
                v = v * 205 / 256;                                  /* чуть темнее, чтобы белый текст читался */
                o |= v << s;
            }
            blur[y * W + x] = o;
        }
    }
    blur_ok = 1;
}

/* ---------- крупный текст: глифы шрифта, растянутые билинейно ---------- */
static i32 big_width(const char *s, i32 sc) { i32 w = 0; for (; *s; s++) w += font_glyphs[FONT_TITLE][(int)(*s - 32)].adv * sc / 256; return w; }
static void big_text(i32 x, i32 y, const char *s, i32 sc, u32 color, u32 alpha)
{
    i32 base = y + font_ascent[FONT_TITLE] * sc / 256;
    for (; *s; s++) {
        const font_glyph_t *g = &font_glyphs[FONT_TITLE][(int)(*s - 32)];
        const u8 *a = &font_alpha[g->off];
        i32 gw = g->w * sc / 256 + 2, gh = g->h * sc / 256 + 2, gx = x + g->x * sc / 256, gy = base + g->y * sc / 256;
        for (i32 dy = 0; dy < gh; dy++)
            for (i32 dx = 0; dx < gw; dx++) {
                i32 u = (dx * 256 + 128) * 256 / sc - 128, v = (dy * 256 + 128) * 256 / sc - 128;
                i32 u0 = u >> 8, v0 = v >> 8, fu = u & 255, fv = v & 255, acc = 0;
                for (int k = 0; k < 4; k++) {
                    i32 ux = u0 + (k & 1), vy = v0 + (k >> 1), wgt = ((k & 1) ? fu : 256 - fu) * ((k >> 1) ? fv : 256 - fv);
                    if (ux >= 0 && ux < g->w && vy >= 0 && vy < g->h) acc += a[vy * g->w + ux] * (wgt >> 4);
                }
                u32 al = (u32)(acc >> 12);
                if (al > 255) al = 255;
                if (al) fb_blend_pixel(gx + dx, gy + dy, color, al * alpha / 255);
            }
        x += g->adv * sc / 256;
    }
}

static int isq(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }
static void avatar(i32 cx, i32 cy, i32 r)
{
    fb_fill_circle(cx, cy, r, RGB(142, 142, 150));
    fb_fill_circle(cx, cy - r / 5, r * 3 / 10, RGB(236, 236, 240));
    i32 bcy = cy + r * 9 / 10, rx = r * 7 / 10, ry = r * 6 / 10;
    for (i32 y = cy + r / 8; y < cy + r; y++) {
        i32 dy = y - cy, ha = isq(r * r - dy * dy), db = y - bcy, hb = 0;
        if (db > -ry && db < ry) hb = rx * isq(ry * ry - db * db) / ry;
        i32 hw = ha < hb ? ha : hb;
        if (hw > 0) fb_fill_rect(cx - hw, y, 2 * hw, 1, RGB(236, 236, 240));
    }
}

static const i8 SN[20] = { 0, 3, 5, 7, 9, 9, 9, 7, 5, 3, 0, -3, -5, -7, -9, -9, -9, -7, -5, -3 };
static int in_gap(int k, int gc, int hw) { int d = (k - gc + 20) % 20; return d <= hw || d >= 20 - hw; }
static void ring_glyph(i32 cx, i32 cy, int gc, int power)
{
    const u32 w = RGB(255, 255, 255);
    for (int k = 0; k < 20; k++) {
        if (in_gap(k, gc, 1)) continue;
        fb_fill_circle(cx + SN[k] * 10 / 9, cy - SN[(k + 5) % 20] * 10 / 9, 1, w);
    }
    if (power) fb_fill_rect(cx - 1, cy - 12, 3, 11, w);
    else {                                                   /* стрелка на конце дуги */
        i32 ax = cx + SN[(gc + 2) % 20] * 10 / 9, ay = cy - SN[(gc + 7) % 20] * 10 / 9;
        fb_fill_rect(ax - 1, ay - 1, 3, 3, w); fb_fill_rect(ax + 1, ay - 5, 5, 2, w); fb_fill_rect(ax + 3, ay - 5, 2, 6, w);
    }
}
static void round_button(i32 cx, i32 cy, const char *label, int power, int hot)
{
    fb_fill_round_rect(cx - 20, cy - 20, 40, 40, 20, RGB(255, 255, 255), hot ? 110 : 64);
    ring_glyph(cx, cy, power ? 0 : 3, power);
    font_draw(cx - font_width(label, FONT_REGULAR) / 2, cy + 28, label, RGB(255, 255, 255), FONT_REGULAR);
}

static const char *const DAYN[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *const MONN[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static int dow(u32 y, u32 m, u32 d) { static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 }; if (m < 3) y--; return (int)((y + y / 4 - y / 100 + y / 400 + (u32)t[m - 1] + d) % 7); }
static char *putu(char *p, u32 v) { char t[10]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) *p++ = t[--n]; return p; }
static char *puts_(char *p, const char *s) { while (*s) *p++ = *s++; return p; }

static i32 shake_off(void)
{
    static const i8 seq[10] = { 0, 14, -14, 11, -11, 7, -7, 3, -2, 0 };
    i32 e = (i32)(now - t_shake);
    if (!t_shake || e < 0 || e >= 45) return 0;
    i32 k = e * 9 / 45, f = (e * 9 * 256 / 45) & 255;
    return (seq[k] * (256 - f) + seq[k + 1] * f) / 256;
}

void login_draw(int have_time, u32 hour, u32 min, u32 day, u32 month, u32 year, int clock24)
{
    if (!active) return;
    i32 W = (i32)fb_width(), H = (i32)fb_height(), cx = W / 2;
    if (!blur_ok) make_blur();
    fb_blit(0, 0, blur, W, H);
    const u32 white = RGB(255, 255, 255);
    /* дата и время */
    i32 sc = H >= 700 ? 1024 : 768;
    char buf[40], *p = buf;
    if (have_time) {
        u32 m = month < 1 ? 1 : month > 12 ? 12 : month;
        p = puts_(p, DAYN[dow(year, m, day)]); *p++ = ' '; p = putu(p, day); *p++ = ' '; p = puts_(p, MONN[m - 1]); *p = 0;
        big_text(cx - big_width(buf, 190) / 2, H / 12 - 4, buf, 190, white, 235);
        p = buf;
        u32 hh = clock24 ? hour : (hour % 12 ? hour % 12 : 12);
        if (clock24 && hh < 10) *p++ = '0';
        p = putu(p, hh); *p++ = ':'; if (min < 10) *p++ = '0'; p = putu(p, min); *p = 0;
        big_text(cx - big_width(buf, sc) / 2, H / 12 + 34, buf, sc, white, 235);
    }
    /* пользователь */
    i32 acy = H * 56 / 100, so = shake_off();
    avatar(cx, acy, 44);
    font_draw(cx - font_width(c_full, FONT_BOLD) / 2, acy + 56, c_full, white, FONT_BOLD);
    i32 fw = 224, fh = 34, fx = cx - fw / 2 + so, fy = acy + 90;
    fb_fill_round_rect(fx, fy, fw, fh, fh / 2, white, 70);
    if (pw_len == 0) {
        font_draw(fx + (fw - font_width("Enter Password", FONT_REGULAR)) / 2, fy + (fh - font_height[FONT_REGULAR]) / 2, "Enter Password", RGB(236, 236, 240), FONT_REGULAR);
    } else {
        int shown = pw_len > 14 ? 14 : pw_len;
        for (int i = 0; i < shown; i++) fb_fill_circle(fx + 18 + i * 11, fy + fh / 2, 3, white);
        i32 bx = fx + fw - fh / 2 - 2, by = fy + fh / 2;
        fb_fill_circle(bx, by, 12, hit_go ? RGB(215, 215, 222) : white);
        for (int k = 0; k < 5; k++) { fb_fill_rect(bx - 5 + k, by - 4 + k, 2, 1, RGB(60, 60, 70)); fb_fill_rect(bx - 5 + k, by + 4 - k, 2, 1, RGB(60, 60, 70)); }
        fb_fill_rect(bx - 6, by, 11, 2, RGB(60, 60, 70));
        for (int k = 0; k < 4; k++) { fb_fill_rect(bx + 1 + k, by - 3 + k, 1, 1, RGB(60, 60, 70)); fb_fill_rect(bx + 1 + k, by + 3 - k, 1, 1, RGB(60, 60, 70)); }
    }
    if (wrong && (i32)(now - t_wrong) < 300)
        font_draw(cx - font_width("Incorrect password", FONT_REGULAR) / 2, fy + fh + 12, "Incorrect password", white, FONT_REGULAR);
    /* кнопки внизу */
    i32 by = H - 90;
    round_button(cx - 56, by, "Restart", 0, hit_rs);
    round_button(cx + 56, by, "Shut Down", 1, hit_sd);
}

/* ---------- ввод ---------- */
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static int check_pw(void)
{
    u8 salt[16]; char out[100];
    for (int i = 0; i < 16; i++) { int a = hexv(c_sh[i * 2]), b = hexv(c_sh[i * 2 + 1]); if (a < 0 || b < 0) return 0; salt[i] = (u8)(a << 4 | b); }
    pw[pw_len] = 0;
    inst_hash_password(pw, salt, out);
    u32 diff = 0; int n = 0;
    for (int i = 0; i < 97; i++) { diff |= (u32)(out[i] ^ c_sh[i]); if (!c_sh[i]) break; n++; }
    return diff == 0 && n == 97;
}
static int submit(void)
{
    if (fading) return 0;
    if (check_pw()) { fading = 1; t_fade = now; fade_a = 1024; for (int i = 0; i < 64; i++) pw[i] = 0; pw_len = 0; return 1; }
    wrong = 1; t_wrong = now; t_shake = now ? now : 1; pw_len = 0; pw[0] = 0;
    return 1;
}

int login_key(int c)
{
    if (!active || fading) return 0;
    if (c == '\n') return pw_len ? submit() : 0;
    if (c == '\b' || c == 0x7f) { if (pw_len) pw[--pw_len] = 0; return 1; }
    if (c == 27) { pw_len = 0; pw[0] = 0; return 1; }
    if (c >= 32 && c < 127 && pw_len < 60) { pw[pw_len++] = (char)c; pw[pw_len] = 0; wrong = 0; return 1; }
    return 0;
}

int login_mouse(i32 x, i32 y, int btn)
{
    if (!active) return 0;
    int moved = x != mx || y != my;
    int click = (btn & 1) && !mprev; mprev = btn & 1;
    {   /* области под курсором: те же формулы, что и при рисовании */
        i32 W = (i32)fb_width(), H = (i32)fb_height(), cx = W / 2, acy = H * 56 / 100, by = H - 90;
        i32 gx = cx - 112 + shake_off() + 224 - 17 - 2, gy = acy + 90 + 17;
        hit_go = pw_len && (x - gx) * (x - gx) + (y - gy) * (y - gy) <= 144;
        hit_rs = (x - (cx - 56)) * (x - (cx - 56)) + (y - by) * (y - by) <= 400;
        hit_sd = (x - (cx + 56)) * (x - (cx + 56)) + (y - by) * (y - by) <= 400;
    }
    mx = x; my = y;
    if (click && !fading) {
        if (hit_go && pw_len) return submit();
        if (hit_rs) { sys_reboot(); return 1; }
        if (hit_sd) { sys_shutdown(); return 1; }
    }
    return moved || click;
}

int login_tick(u32 ticks)
{
    if (!active) return 0;
    now = ticks; last = ticks;
    int r = 0;
    if (shake_off() || (t_shake && (i32)(now - t_shake) < 50)) r = 1;
    if (wrong && (i32)(now - t_wrong) < 320) r = 1;
    if (fading) {
        i32 e = (i32)(now - t_fade);
        fade_a = 1024 - e * 1024 / 40;
        if (fade_a <= 0) { fade_a = 0; active = 0; fading = 0; }
        r = 1;
    }
    return r;
}
