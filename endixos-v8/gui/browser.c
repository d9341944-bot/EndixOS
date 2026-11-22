/* Браузер EndixOS: окно с адресной строкой и страницей. Страница грузится из сети (kernel/net.c)
 * или берётся из встроенных адресов endix://. Всё движется плавно: прогресс, прокрутка, полоса, подсказка ссылки. */
#include "settings.h"
#include "browser.h"
#include "html.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "ui.h"
#include "anim.h"
#include "img.h"
#include "photos.h"
#include "../kernel/blk.h"
#include "../kernel/net.h"
#include "../kernel/event.h"

#define TB 46
#define PAGE_BUF (120 * 1024)
#define HIST_N 24
#define URLMAX 250

static u8 pagebuf[PAGE_BUF];
#define IMG_BUF (700 * 1024)
#define IPOOL   (3 * 1024 * 1024)                  /* слов: ~12 МБ готовых пикселей на страницу */
static u8 imgbuf[IMG_BUF];
static u32 ipool[IPOOL]; static u32 ipool_n;
static struct { u32 off; i16 w, h; i32 a; } idec[32];
static int iq_cur = -1, iq_next = 1 << 20;        /* какая картинка качается / какая следующая */
static u32 page_bg_c = 0xFFFFFF; static int page_dark;
static void img_reset(void);
static void clipped_rect(i32 x, i32 y, i32 w, i32 h, i32 y0, i32 y1, u32 c);
static char url[URLMAX + 6];                       /* показанный адрес */
static char loading_url[URLMAX + 6];
static char ed[URLMAX + 6]; static int ed_n, ed_pos, editing, sel_all;
static char hist[HIST_N][URLMAX + 6]; static i32 hist_sc[HIST_N]; static int hist_n, hist_pos;
static i32 vw = BROWSER_W, vh = BROWSER_H - 28 - TB;      /* вся область содержимого: vw x (vh + TB) */
static i32 lay_w = -1;
static i32 sc, sc_t;                               /* прокрутка: текущая и целевая, пиксели */
static i32 prog, prog_t, prog_a;                   /* полоса загрузки 0..1024, прозрачность 0..1024 */
static i32 hv[4], hv_t[4];                         /* подсветка кнопок: назад, вперёд, обновить, адрес */
static i32 sb_a, sb_a_t, st_a;                     /* полоса прокрутки и подсказка ссылки */
static i32 fade;                                   /* осветление страницы при появлении: 1024 -> 0 */
static int loading, link_hover = -1, sb_drag; static i32 sb_grab;
static u32 now, last_anim, load_t0, last_scroll_t;
static char status_txt[URLMAX + 6];
static int have_page;
static char errhtml[1400];
static int restore_sc;
static i32 pending_sc;
static u32 page_len; static int page_plain, src_view;      /* исходник последней загруженной страницы (для Ctrl+U) */

static int home_mode, home_hover = -1;                  /* нативная стартовая страница */
static int gen_wp;                                      /* загружается ответ API обоев: из JSON строим страницу */
static char gen[32768];
#define DL_BUF (8u << 20)
static u8 dlbuf[DL_BUF];
static u32 dlpix[1280 * 720];
static u8 pngbuf[1280 * 720 * 3 + 720 + (1280 * 720 * 3 / 65535 + 3) * 5 + 128];
static int dl_active; static char dl_id[16]; static u32 dl_t0;
static char toast[96]; static u32 toast_t; static i32 toast_a;
__attribute__((weak)) void wm_notify(const char *title, const char *text) { (void)title; (void)text; }

/* ---------- встроенные страницы ---------- */

static const char HOME_HTML[] =
    "<title>Start Page</title>"
    "<center><h1>Browix</h1>"
    "<p>A browser written from scratch: its own network stack, HTML parser and page layout.</p></center>"
    "<hr><h2>Try these</h2>"
    "<ul>"
    "<li><a href=\"https://en.wikipedia.org/wiki/Special:Random\">Wikipedia</a> - a random article</li>"
    "<li><a href=\"https://lite.duckduckgo.com/lite/\">DuckDuckGo Lite</a> - web search</li>"
    "<li><a href=\"http://example.com/\">example.com</a> - the classic test page</li>"
    "<li><a href=\"http://info.cern.ch/\">info.cern.ch</a> - the first website ever</li>"
    "<li><a href=\"https://news.ycombinator.com/\">Hacker News</a></li>"
    "<li><a href=\"endix://about\">About this browser</a></li>"
    "</ul>"
    "<h2>How to use</h2>"
    "<p>Type an address in the bar above. Type a question or any words and press Enter to search the web (DuckDuckGo). "
    "Scroll with the wheel, arrows, Page Up/Down, Space. Backspace goes back. Ctrl+L focuses the address bar, Ctrl+R reloads, Ctrl+U shows the page source.</p>"
    "<p>HTTPS works (TLS 1.2), but the server certificate is not verified yet. "
    "Pages are shown without CSS, scripts and images, so heavy modern sites may look plain or incomplete.</p>";

static const char ABOUT_HTML[] =
    "<title>About Browix</title>"
    "<h1>About</h1>"
    "<p>This browser is part of EndixOS " ENDIX_VERSION ". Everything underneath is written from scratch:</p>"
    "<ul><li>Intel e1000 network card driver</li><li>Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS</li>"
    "<li>TCP client, HTTP/1.0 with redirects and chunked transfer, TLS 1.2 (X25519, AES-GCM)</li><li>HTML tokenizer and page layout</li></ul>"
    "<h2>Supported</h2>"
    "<p>Headings, paragraphs, lists, links, <b>bold</b>, <code>code</code>, preformatted text, quotes, rules, simple tables (as rows of text), "
    "UTF-8 with Latin and Cyrillic letters.</p>"
    "<h2>Not yet</h2>"
    "<p>CSS, JavaScript, images, forms, certificate checks.</p>"
    "<p><a href=\"endix://home\">Back to the start page</a></p>";

static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s, u32 cap) { u32 i = 0; while (s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int starts(const char *s, const char *p) { while (*p && *s == *p) { s++; p++; } return !*p; }

/* ---------- построение страниц-сообщений ---------- */

static u32 eapp(u32 n, const char *s, int escape)
{
    for (; *s && n + 8 < sizeof(errhtml); s++) {
        if (escape && *s == '<') { errhtml[n++] = '&'; errhtml[n++] = 'l'; errhtml[n++] = 't'; errhtml[n++] = ';'; }
        else if (escape && *s == '&') { errhtml[n++] = '&'; errhtml[n++] = 'a'; errhtml[n++] = 'm'; errhtml[n++] = 'p'; errhtml[n++] = ';'; }
        else if (escape && *s == '"') { errhtml[n++] = '%'; errhtml[n++] = '2'; errhtml[n++] = '2'; }
        else errhtml[n++] = *s;
    }
    errhtml[n] = 0;
    return n;
}

static void show_html(const char *src, u32 len, const char *base, int plain);
static void set_toast(const char *t) { scpy(toast, t, sizeof toast); toast_t = now; toast_a = 1024; }

#define NTILE 8
static const struct { const char *name, *url; u32 col; char glyph; } tiles[NTILE] = {
    { "Wallpapers", "endix://wallpapers", 0xFF9500, 'p' }, { "Random", "endix://wallpapers?s=random", 0xAF52DE, '?' },
    { "Wikipedia", "https://en.wikipedia.org/wiki/Main_Page", 0x636366, 'W' }, { "DuckDuckGo", "https://lite.duckduckgo.com/lite/", 0xFF5E3A, 'D' },
    { "Hacker News", "https://news.ycombinator.com/", 0xFF6600, 'Y' }, { "example.com", "http://example.com/", 0x0A84FF, 'E' },
    { "First website", "http://info.cern.ch/", 0x30B0C7, 'C' }, { "About Browix", "endix://about", 0x34C759, 'i' } };
#define TILE_W 120
#define TILE_H 118

static void home_geom(i32 *gx, i32 *gy, i32 *cols, i32 *sx, i32 *sy, i32 *sw)
{
    i32 c = (vw - 40) / TILE_W; if (c > 4) c = 4; if (c < 1) c = 1;
    *cols = c; *gx = (vw - c * TILE_W) / 2; *gy = 168;
    *sw = vw - 60 < 520 ? vw - 60 : 520; *sx = (vw - *sw) / 2; *sy = 56;
}
static int home_tile_at(i32 rx, i32 ry)
{
    i32 gx, gy, c, sx, sy, sw; home_geom(&gx, &gy, &c, &sx, &sy, &sw);
    ry -= TB;
    if (rx < gx || ry < gy) return -1;
    i32 col = (rx - gx) / TILE_W, row = (ry - gy) / TILE_H;
    if (col >= c) return -1;
    int i = (int)(row * c + col);
    return i < NTILE ? i : -1;
}
static void tile_icon(i32 x, i32 y, i32 s, int i, u32 col)
{
    fb_fill_round_rect(x, y, s, s, s / 4, col, 255);
    u32 wh = RGB(255, 255, 255);
    if (tiles[i].glyph == 'p') {                                   /* картинка: солнце и горы */
        fb_fill_circle(x + s * 68 / 100, y + s * 32 / 100, s / 9, wh);
        for (i32 k = 0; k < s * 28 / 100; k++) { fb_fill_rect(x + s * 14 / 100 + (s * 28 / 100 - k) * 6 / 5 - 2 * 0, y + s * 50 / 100 + k, 2 * k * 6 / 5 + 2, 1, wh); }
        for (i32 k = 0; k < s * 20 / 100; k++) fb_fill_rect(x + s * 48 / 100 + (s * 20 / 100 - k), y + s * 58 / 100 + k, 2 * k + 2, 1, RGB(255, 230, 190));
    } else {
        char g[2] = { tiles[i].glyph, 0 };
        i32 w = font_width(g, FONT_TITLE);
        font_draw(x + (s - w) / 2, y + s / 2 - 12, g, wh, FONT_TITLE);
    }
}
static void home_draw(i32 x, i32 py)
{
    const theme_t *th = theme();
    int dk = theme_is_dark();
    i32 gx, gy, c, sx, sy, sw; home_geom(&gx, &gy, &c, &sx, &sy, &sw);
    font_draw(x + (vw - font_width("Browix", FONT_TITLE)) / 2, py + 14, "Browix", th->text, FONT_TITLE);
    fb_fill_round_rect(x + sx, py + sy, sw, 40, 12, dk ? RGB(58, 58, 64) : RGB(255, 255, 255), 255);
    fb_fill_circle(x + sx + 22, py + sy + 18, 7, th->sub); fb_fill_circle(x + sx + 22, py + sy + 18, 5, dk ? RGB(58, 58, 64) : RGB(255, 255, 255));
    for (i32 k = 0; k < 5; k++) fb_fill_rect(x + sx + 26 + k, py + sy + 22 + k, 2, 2, th->sub);
    font_draw(x + sx + 40, py + sy + 11, "Search or enter website name", th->sub, FONT_REGULAR);
    font_draw(x + gx + 4, py + gy - 34, "Favorites", th->text, FONT_BOLD);
    for (int i = 0; i < NTILE; i++) {
        i32 tx = x + gx + (i % c) * TILE_W, ty = py + gy + (i / c) * TILE_H;
        if (i == home_hover) fb_fill_round_rect(tx + 2, ty - 4, TILE_W - 4, TILE_H - 4, 14, dk ? RGB(255, 255, 255) : RGB(0, 0, 0), 22);
        tile_icon(tx + (TILE_W - 64) / 2, ty + 4, 64, i, tiles[i].col | 0);
        i32 w = font_width(tiles[i].name, FONT_REGULAR);
        font_draw(tx + (TILE_W - w) / 2, ty + 78, tiles[i].name, th->text, FONT_REGULAR);
    }
    i32 ny = py + gy + ((NTILE + c - 1) / c) * TILE_H + 8;
    if (ny + 20 < py + vh) font_draw(x + gx + 4, ny, "Wallpapers: a Wallhaven catalog. Type wh cats in the address bar to search.", th->sub, FONT_REGULAR);
}

/* ---------- обои: Wallhaven API -> страница ---------- */

static u32 gapp(u32 n, const char *t) { while (*t && n + 1 < sizeof gen) gen[n++] = *t++; gen[n] = 0; return n; }
static const char *jfind(const char *p, const char *end, const char *key)      /* значение строки после "key":" */
{
    u32 kl = slen(key);
    for (; p + kl + 4 < end; p++) {
        if (p[0] != '"') continue;
        u32 i = 0; while (i < kl && p[1 + i] == key[i]) i++;
        if (i == kl && p[1 + kl] == '"' && p[2 + kl] == ':' && p[3 + kl] == '"') return p + 4 + kl;
    }
    return 0;
}
static u32 jstr(const char *p, char *out, u32 cap)                            /* до закрывающей кавычки, \/ -> / */
{
    u32 o = 0;
    while (*p && *p != '"' && o + 1 < cap) { if (*p == '\\' && p[1]) p++; out[o++] = *p++; }
    out[o] = 0; return o;
}
static int url_param(const char *u, const char *key, char *out, u32 cap)
{
    out[0] = 0;
    const char *q = u; while (*q && *q != '?') q++;
    if (!*q) return 0;
    u32 kl = slen(key);
    for (q++; *q; ) {
        if (starts(q, key) && q[kl] == '=') { u32 o = 0; q += kl + 1; while (*q && *q != '&' && o + 1 < cap) out[o++] = *q++; out[o] = 0; return 1; }
        while (*q && *q != '&') q++;
        if (*q == '&') q++;
    }
    return 0;
}
static int is_safe_q(const char *q) { for (; *q; q++) if (!((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9') || *q == '+' || *q == '-' || *q == '_' || *q == '.' || *q == '%')) return 0; return 1; }

typedef struct { char q[64], s[16], t[4], c[4], r[8], a[8]; int p; } wp_t;
static const char *const WS_[] = { "toplist", "date_added", "random", "views", "favorites", "relevance" };
static const char *const WS_N[] = { "Toplist", "Latest", "Random", "Most viewed", "Favorites", "Relevance" };
static const char *const WT_[] = { "1d", "3d", "1w", "1M", "3M", "6M", "1y" };
static const char *const WT_N[] = { "Day", "3 days", "Week", "Month", "3 months", "6 months", "Year" };
static const char *const WR_[] = { "fit", "720", "1080", "1366", "1280", "any" };
static const char *const WR_N[] = { "Fits netbook", "1280x720", "1920x1080", "1366x768", "1280x800", "Any (big files)" };
static const char *const WRES[] = { "1920x1080,1366x768,1280x720,1280x800,1600x900,1024x768", "1280x720", "1920x1080", "1366x768", "1280x800", "" };
static const char *const WA_[] = { "0", "16x9", "16x10", "4x3", "21x9" };
static const char *const WA_N[] = { "Any ratio", "16:9", "16:10", "4:3", "21:9" };
static int pick(const char *v, const char *const *list, int n) { for (int i = 0; i < n; i++) if (seq(v, list[i])) return i; return -1; }

static void wp_parse(const char *u, wp_t *w)
{
    char pg[8];
    url_param(u, "q", w->q, sizeof w->q); url_param(u, "s", w->s, sizeof w->s); url_param(u, "t", w->t, sizeof w->t);
    url_param(u, "c", w->c, sizeof w->c); url_param(u, "r", w->r, sizeof w->r); url_param(u, "a", w->a, sizeof w->a); url_param(u, "p", pg, sizeof pg);
    if (!is_safe_q(w->q)) w->q[0] = 0;
    if (pick(w->s, WS_, 6) < 0) scpy(w->s, w->q[0] ? "relevance" : "toplist", sizeof w->s);
    if (pick(w->t, WT_, 7) < 0) scpy(w->t, "1M", sizeof w->t);
    if (pick(w->r, WR_, 6) < 0) scpy(w->r, "fit", sizeof w->r);
    if (pick(w->a, WA_, 5) < 0) scpy(w->a, "0", sizeof w->a);
    int ok = slen(w->c) == 3; for (int i = 0; ok && i < 3; i++) if (w->c[i] != '0' && w->c[i] != '1') ok = 0;
    if (!ok || seq(w->c, "000")) scpy(w->c, "111", sizeof w->c);
    int p = 0; for (int i = 0; pg[i] >= '0' && pg[i] <= '9' && i < 4; i++) p = p * 10 + (pg[i] - '0');
    w->p = p < 1 ? 1 : p > 9999 ? 9999 : p;
}
static u32 wp_href(char *o, u32 cap, const wp_t *w)
{
    char t[8]; int v = w->p, l = 0, d[5], n = 0;
    do { d[n++] = v % 10; v /= 10; } while (v);
    while (n) t[l++] = (char)('0' + d[--n]);
    t[l] = 0;
    scpy(o, "endix://wallpapers?s=", cap); u32 k = slen(o);
    const char *parts[] = { w->s, "&t=", w->t, "&c=", w->c, "&r=", w->r, "&a=", w->a, "&p=", t, w->q[0] ? "&q=" : "", w->q };
    for (u32 i = 0; i < sizeof parts / sizeof *parts; i++) { scpy(o + k, parts[i], cap - k); k = slen(o); }
    return k;
}
static void wp_api_url(const char *u, char *out, u32 cap)
{
    wp_t w; wp_parse(u, &w);
    char pg[8]; { int v = w.p, l = 0, d[5], n = 0; do { d[n++] = v % 10; v /= 10; } while (v); while (n) pg[l++] = (char)('0' + d[--n]); pg[l] = 0; }
    scpy(out, "https://wallhaven.cc/api/v1/search?purity=100&order=desc&categories=", cap);
    const char *parts[] = { w.c, "&sorting=", w.s, "&page=", pg };
    for (u32 i = 0; i < sizeof parts / sizeof *parts; i++) { u32 k = slen(out); scpy(out + k, parts[i], cap - k); }
    u32 k = slen(out);
    if (seq(w.s, "toplist")) { scpy(out + k, "&topRange=", cap - k); k = slen(out); scpy(out + k, w.t, cap - k); k = slen(out); }
    int ri = pick(w.r, WR_, 6);
    if (ri == 5) { scpy(out + k, "&atleast=1024x600", cap - k); k = slen(out); }
    else { scpy(out + k, "&resolutions=", cap - k); k = slen(out); scpy(out + k, WRES[ri], cap - k); k = slen(out); }
    if (!seq(w.a, "0")) { scpy(out + k, "&ratios=", cap - k); k = slen(out); scpy(out + k, w.a, cap - k); k = slen(out); }
    if (w.q[0]) { scpy(out + k, "&q=", cap - k); scpy(out + k + 3, w.q, cap - k - 3); }
}
static long jnum(const char *p, const char *end, const char *key)
{
    u32 kl = slen(key);
    for (; p + kl + 4 < end; p++) {
        if (p[0] != '"') continue;
        u32 i = 0; while (i < kl && p[1 + i] == key[i]) i++;
        if (i == kl && p[1 + kl] == '"' && p[2 + kl] == ':') { const char *q = p + 3 + kl; long v = 0; if (*q < '0' || *q > '9') return -1; while (*q >= '0' && *q <= '9') v = v * 10 + (*q++ - '0'); return v; }
    }
    return -1;
}
static u32 napp(u32 n, long v) { char b[16]; int l = 0, d[12], k = 0; if (v < 0) v = 0; do { d[k++] = (int)(v % 10); v /= 10; } while (v); while (k) { b[l++] = (char)('0' + d[--k]); if (k % 3 == 0 && k) b[l++] = ','; } b[l] = 0; return gapp(n, b); }
static u32 wp_row(u32 n, const char *label, const char *const *names, const char *const *vals, int cnt, const char *cur, const wp_t *w, char which)
{
    n = gapp(n, "<b>"); n = gapp(n, label); n = gapp(n, ":</b> ");
    for (int i = 0; i < cnt; i++) {
        wp_t x = *w; x.p = 1;
        if (which == 's') scpy(x.s, vals[i], sizeof x.s); else if (which == 't') scpy(x.t, vals[i], sizeof x.t);
        else if (which == 'r') scpy(x.r, vals[i], sizeof x.r); else scpy(x.a, vals[i], sizeof x.a);
        if (seq(cur, vals[i])) { n = gapp(n, "[<b>"); n = gapp(n, names[i]); n = gapp(n, "</b>] "); continue; }
        char h[300]; wp_href(h, sizeof h, &x);
        n = gapp(n, "<a href=\"") ; n = gapp(n, h); n = gapp(n, "\">"); n = gapp(n, names[i]); n = gapp(n, "</a> ");
    }
    return gapp(n, "<br>");
}

static void wp_page_html(const char *json, u32 len, const char *u)
{
    wp_t w; wp_parse(u, &w);
    char id[16], res[20], path[160], th[160], h[300];
    const char *end = json + len, *p = json;
    long total = jnum(json, end, "total"), last = jnum(json, end, "last_page");
    u32 n = gapp(0, "<title>Wallpapers</title><h1>Wallpapers</h1><p>Wallhaven catalog (SFW). Search: type <b>wh</b> and a word in the address bar, e.g. <b>wh mountains</b>. Click a picture to save it into Pictures.</p><p>");
    n = wp_row(n, "Sort", WS_N, WS_, 6, w.s, &w, 's');
    if (seq(w.s, "toplist")) n = wp_row(n, "Range", WT_N, WT_, 7, w.t, &w, 't');
    n = gapp(n, "<b>Categories:</b> ");
    { static const char *cn[3] = { "General", "Anime", "People" };
      for (int i = 0; i < 3; i++) { wp_t x = w; x.p = 1; x.c[i] = x.c[i] == '1' ? '0' : '1'; if (seq(x.c, "000")) x.c[i] = '1';
        wp_href(h, sizeof h, &x); n = gapp(n, w.c[i] == '1' ? "[<b>" : ""); n = gapp(n, "<a href=\""); n = gapp(n, h); n = gapp(n, "\">"); n = gapp(n, cn[i]); n = gapp(n, "</a>"); n = gapp(n, w.c[i] == '1' ? "</b>] " : " "); } }
    n = gapp(n, "<br>");
    n = wp_row(n, "Size", WR_N, WR_, 6, w.r, &w, 'r');
    n = wp_row(n, "Ratio", WA_N, WA_, 5, w.a, &w, 'a');
    n = gapp(n, "<b>Tags:</b> ");
    { static const char *const tg[] = { "nature", "space", "cars", "anime", "city", "minimal", "abstract", "dark", "mountains", "ocean", "forest", "cyberpunk", "neon", "sunset", "cats", "landscape", "gaming", "architecture", "flowers", "stars", "retro", "japan", "winter", "desert" };
      for (u32 i = 0; i < sizeof tg / sizeof *tg; i++) { wp_t x = w; x.p = 1; scpy(x.q, tg[i], sizeof x.q); if (seq(x.s, "toplist") && 0) {} wp_href(h, sizeof h, &x); n = gapp(n, "<a href=\""); n = gapp(n, h); n = gapp(n, "\">"); n = gapp(n, tg[i]); n = gapp(n, "</a> "); } }
    if (w.q[0]) { n = gapp(n, "<br><b>Search:</b> "); n = gapp(n, w.q); n = gapp(n, " "); wp_t x = w; x.p = 1; x.q[0] = 0; if (seq(x.s, "relevance")) scpy(x.s, "toplist", sizeof x.s); wp_href(h, sizeof h, &x); n = gapp(n, "<a href=\""); n = gapp(n, h); n = gapp(n, "\">clear</a>"); }
    n = gapp(n, "</p><p>");
    if (total >= 0) { n = napp(n, total); n = gapp(n, " wallpapers"); if (last > 0) { n = gapp(n, ", page "); n = napp(n, w.p); n = gapp(n, " of "); n = napp(n, last); } n = gapp(n, "<br>"); }
    int shown = 0;
    while (shown < 24 && (p = jfind(p, end, "id")) != 0) {
        jstr(p, id, sizeof id);
        const char *nx = jfind(p, end, "id"); const char *lim = nx ? nx : end;
        const char *r = jfind(p, lim, "resolution"), *pa = jfind(p, lim, "path"), *sm = jfind(p, lim, "small");
        p = nx ? nx - 6 : end;                                   /* на ключ следующего "id" */
        if (!pa || !sm || !id[0]) continue;
        jstr(pa, path, sizeof path); jstr(sm, th, sizeof th);
        if (r) jstr(r, res, sizeof res); else res[0] = 0;
        for (u32 k = 0; id[k]; k++) if (!((id[k] >= 'a' && id[k] <= 'z') || (id[k] >= '0' && id[k] <= '9'))) id[0] = 0;
        if (!id[0] || !starts(path, "https://") || !starts(th, "https://")) continue;
        n = gapp(n, "<a href=\"endix://dl?id="); n = gapp(n, id); n = gapp(n, "&u="); n = gapp(n, path); n = gapp(n, "\"><img src=\""); n = gapp(n, th);
        n = gapp(n, "\" width=\"196\" alt=\""); n = gapp(n, res); n = gapp(n, "\"></a> ");
        shown++;
    }
    if (!shown) n = gapp(n, "Nothing found. Try another word, category or size.");
    n = gapp(n, "</p><p>");
    if (last < 1) last = w.p + 1;
    if (last > 9999) last = 9999;
    {   int from = w.p - 3 < 1 ? 1 : w.p - 3, to = w.p + 3 > last ? (int)last : w.p + 3;
        wp_t x = w;
        if (w.p > 1) { x.p = w.p - 1; wp_href(h, sizeof h, &x); n = gapp(n, "<a href=\""); n = gapp(n, h); n = gapp(n, "\">Previous</a> "); }
        if (from > 1) { x.p = 1; wp_href(h, sizeof h, &x); n = gapp(n, "<a href=\""); n = gapp(n, h); n = gapp(n, "\">1</a> ... "); }
        for (int i = from; i <= to; i++) {
            if (i == w.p) { n = gapp(n, "[<b>"); n = napp(n, i); n = gapp(n, "</b>] "); continue; }
            x.p = i; wp_href(h, sizeof h, &x); n = gapp(n, "<a href=\""); n = gapp(n, h); n = gapp(n, "\">"); n = napp(n, i); n = gapp(n, "</a> ");
        }
        if (to < last) { n = gapp(n, "... "); x.p = (int)last; wp_href(h, sizeof h, &x); n = gapp(n, "<a href=\""); n = gapp(n, h); n = gapp(n, "\">"); n = napp(n, last); n = gapp(n, "</a> "); }
        if (w.p < last) { x.p = w.p + 1; wp_href(h, sizeof h, &x); n = gapp(n, "<a href=\""); n = gapp(n, h); n = gapp(n, "\">Next</a>"); }
    }
    n = gapp(n, "</p>");
    show_html(gen, n, u, 0);
}

static u8 *be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; return p + 4; }
static u32 crc_t[256]; static int crc_ok;
static u32 crc_upd(u32 c, const u8 *d, u32 n)
{
    if (!crc_ok) { for (u32 i = 0; i < 256; i++) { u32 x = i; for (int k = 0; k < 8; k++) x = x & 1 ? 0xEDB88320u ^ (x >> 1) : x >> 1; crc_t[i] = x; } crc_ok = 1; }
    for (u32 i = 0; i < n; i++) c = crc_t[(c ^ d[i]) & 255] ^ (c >> 8);
    return c;
}
static u8 *png_chunk_end(u8 *start, u8 *p) { be32(start, (u32)(p - start - 8)); return be32(p, ~crc_upd(0xFFFFFFFFu, start + 4, (u32)(p - start - 4))); }

/* PNG без сжатия (deflate stored): просто и достаточно для обоев. Возвращает размер файла. */
static u32 png_encode(const u32 *px, i32 w, i32 h)
{
    u8 *p = pngbuf; static const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    for (int i = 0; i < 8; i++) *p++ = sig[i];
    u8 *c = p; p += 4; *p++ = 'I'; *p++ = 'H'; *p++ = 'D'; *p++ = 'R';
    p = be32(p, (u32)w); p = be32(p, (u32)h); *p++ = 8; *p++ = 2; *p++ = 0; *p++ = 0; *p++ = 0;
    p = png_chunk_end(c, p);
    u32 raw = (u32)h * (3 * (u32)w + 1), left = raw, a = 1, b = 0, row = 0, col = 0; int ph = 0;
    c = p; p += 4; *p++ = 'I'; *p++ = 'D'; *p++ = 'A'; *p++ = 'T'; *p++ = 0x78; *p++ = 0x01;
    while (left) {
        u32 n = left > 65535 ? 65535 : left;
        *p++ = n == left; *p++ = (u8)n; *p++ = (u8)(n >> 8); *p++ = (u8)~n; *p++ = (u8)(~n >> 8);
        for (u32 i = 0; i < n; i++) {
            u8 v;
            if (ph == 0) { v = 0; ph = 1; col = 0; }
            else { u32 q = px[row * (u32)w + col / 3], ch = col % 3; v = ch == 0 ? (u8)(q >> 16) : ch == 1 ? (u8)(q >> 8) : (u8)q; if (++col == 3 * (u32)w) { ph = 0; row++; } }
            *p++ = v; a += v; if (a >= 65521) a -= 65521; b += a; if (b >= 65521) b -= 65521;
        }
        left -= n;
    }
    p = be32(p, (b << 16) | a);
    p = png_chunk_end(c, p);
    c = p; p += 4; *p++ = 'I'; *p++ = 'E'; *p++ = 'N'; *p++ = 'D';
    p = png_chunk_end(c, p);
    return (u32)(p - pngbuf);
}

static void dl_finish(u32 len)
{
    int w = 0, h = 0;
    if (!img_info(dlbuf, len, &w, &h) || w < 16 || h < 16) { set_toast("This file is not a supported picture (baseline JPEG or PNG)."); return; }
    int tw = w, th = h;
    if (tw > 1280) { th = th * 1280 / tw; tw = 1280; }
    if (th > 720) { tw = tw * 720 / th; th = 720; }
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    if (!img_decode_ex(dlbuf, len, tw, th, dlpix, 4u * 1000 * 1000)) { set_toast("Could not decode this picture."); return; }
    u32 sz = png_encode(dlpix, tw, th);
    static fat_t f;
    if (!photos_volume(&f)) { set_toast("No disk to save to (install EndixOS first)."); return; }
    fat_mkdir(&f, "/Pictures");
    char path[40] = "/Pictures/WH"; u32 l = 12;
    for (u32 i = 0; dl_id[i] && l < 20; i++) path[l++] = (char)(dl_id[i] >= 'a' && dl_id[i] <= 'z' ? dl_id[i] - 32 : dl_id[i]);
    path[l++] = '.'; path[l++] = 'P'; path[l++] = 'N'; path[l++] = 'G'; path[l] = 0;
    if (fat_write(&f, path, pngbuf, sz) != 0) { set_toast("Could not write to the disk."); return; }
    char m[96] = "Saved to Pictures: "; scpy(m + 19, path + 10, sizeof m - 19);
    set_toast(m); wm_notify("Wallpapers", m);
}

static void start_dl(const char *u)
{
    char id[16], pu[200];
    url_param(u, "id", id, sizeof id);
    const char *q = u; while (*q && !(q[0] == '&' && q[1] == 'u' && q[2] == '=')) q++;
    if (!*q) return;
    scpy(pu, q + 3, sizeof pu);
    if (!starts(pu, "https://") || !id[0]) return;
    if (dl_active) { set_toast("Already downloading..."); return; }
    if (loading || iq_cur >= 0 || iq_next < html_image_count()) { set_toast("Wait until the thumbnails finish loading."); return; }
    scpy(dl_id, id, sizeof dl_id);
    if (net_fetch(pu, dlbuf, DL_BUF) < 0) { net_fetch_t f; net_fetch_info(&f); set_toast(f.err[0] ? f.err : "Download failed."); return; }
    dl_active = 1; dl_t0 = now; set_toast("Downloading...");
}

static void show_html(const char *src, u32 len, const char *base, int plain)
{
    html_load(src, len, base, plain);
    page_bg_c = html_page_bg(); page_dark = ((page_bg_c >> 16 & 255) * 3 + (page_bg_c >> 8 & 255) * 6 + (page_bg_c & 255)) / 10 < 110;
    img_reset();
    lay_w = -1;
    have_page = 1;
    sc = sc_t = 0;
    fade = 1024;
}

static void show_error(const char *head, const char *msg, const char *retry_url)
{
    u32 n = 0;
    n = eapp(n, "<title>", 0); n = eapp(n, head, 1); n = eapp(n, "</title><h1>", 0); n = eapp(n, head, 1);
    n = eapp(n, "</h1><p>", 0); n = eapp(n, msg, 1); n = eapp(n, "</p>", 0);
    if (head[0] == 'C') {                                  /* строка про сеть: сразу видно, есть ли адрес и DNS */
        net_info_t ni; char ip[20];
        net_info(&ni);
        n = eapp(n, "<p>Network: ", 0);
        if (!ni.present) n = eapp(n, "no network adapter is active.", 0);
        else if (!ni.up) n = eapp(n, "no IP address yet (cable or Wi-Fi is not connected, or DHCP is still running).", 0);
        else {
            n = eapp(n, "address ", 0); net_ip_fmt(ni.ip, ip); n = eapp(n, ip, 1);
            n = eapp(n, ", gateway ", 0); net_ip_fmt(ni.gw, ip); n = eapp(n, ip, 1);
            n = eapp(n, ", DNS ", 0); net_ip_fmt(ni.dns, ip); n = eapp(n, ip, 1);
        }
        n = eapp(n, "</p>", 0);
    }
    if (retry_url && retry_url[0]) {
        n = eapp(n, "<p><a href=\"", 0); n = eapp(n, retry_url, 1); n = eapp(n, "\">Try again</a></p>", 0);
    }
    n = eapp(n, "<p><a href=\"endix://home\">Start page</a></p>", 0);
    show_html(errhtml, n, "endix://error", 0);
}

/* ---------- адреса ---------- */

static int looks_like_host(const char *s)
{
    int dot = 0, i = 0;
    for (; s[i] && s[i] != '/' && s[i] != '?'; i++) {
        if (s[i] == ' ') return 0;
        if (s[i] == '.') dot = 1;
    }
    if (starts(s, "localhost")) return 1;
    return dot;
}

/* Превращает набранное в адрес: схема есть - как есть; похоже на сайт - http://; иначе поиск. */
static void normalize(const char *in, char *out, u32 cap)
{
    while (*in == ' ') in++;
    if (starts(in, "http://") || starts(in, "https://") || starts(in, "endix://")) { scpy(out, in, cap); return; }
    if ((in[0] == 'w' && in[1] == 'h' && in[2] == ' ') || starts(in, "wallhaven ")) {          /* wh кошки -> обои */
        while (*in && *in != ' ') in++;
        while (*in == ' ') in++;
        scpy(out, "endix://wallpapers?q=", cap);
        u32 l = slen(out);
        for (; *in && l + 4 < cap && l < 20 + 60; in++) { u8 c = (u8)*in; out[l++] = c == ' ' ? '+' : ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') ? (char)c : '-'; }
        out[l] = 0;
        return;
    }
    if (looks_like_host(in)) {
        scpy(out, "http://", cap);
        u32 l = slen(out);
        scpy(out + l, in, cap - l);
        return;
    }
    scpy(out, "https://lite.duckduckgo.com/lite/?q=", cap);
    u32 l = slen(out);
    for (; *in && l + 4 < cap; in++) {
        u8 c = (u8)*in;
        if (c == ' ') out[l++] = '+';
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') out[l++] = (char)c;
        else { static const char hx[] = "0123456789ABCDEF"; out[l++] = '%'; out[l++] = hx[c >> 4]; out[l++] = hx[c & 15]; }
    }
    out[l] = 0;
}

/* ---------- загрузка ---------- */

static void start_load(const char *u, i32 scroll_to)
{
    net_fetch_abort();
    iq_cur = -1; iq_next = 1 << 20;
    loading = 0;
    scpy(url, u, sizeof(url));
    pending_sc = scroll_to;
    restore_sc = scroll_to > 0;
    home_mode = 0; gen_wp = 0;
    if (seq(u, "endix://home") || seq(u, "endix://")) {
        show_html("<title>Start Page</title>", 25, "endix://home", 0); home_mode = 1; home_hover = -1;
        page_bg_c = theme_is_dark() ? RGB(30, 30, 34) : RGB(245, 245, 247); page_dark = theme_is_dark();
        prog = 0; prog_a = 0; return;
    }
    if (starts(u, "endix://wallpapers")) {
        char api[400]; wp_api_url(u, api, sizeof api);
        scpy(loading_url, u, sizeof(loading_url));
        if (net_fetch(api, pagebuf, PAGE_BUF) < 0) { net_fetch_t f; net_fetch_info(&f); show_error("Can't open the page", f.err[0] ? f.err : "Unknown error.", u); prog_a = 0; return; }
        page_len = 0; src_view = 0; gen_wp = 1;
        loading = 1; load_t0 = now; prog = 0; prog_t = 220; prog_a = 1024;
        return;
    }
    if (seq(u, "endix://about")) { show_html(ABOUT_HTML, sizeof(ABOUT_HTML) - 1, "endix://about", 0); prog = 0; prog_a = 0; return; }
    if (starts(u, "endix://")) { show_error("Page not found", "There is no such built-in page.", 0); return; }
    scpy(loading_url, u, sizeof(loading_url));
    if (net_fetch(u, pagebuf, PAGE_BUF) < 0) {
        net_fetch_t f; net_fetch_info(&f);
        show_error("Can't open the page", f.err[0] ? f.err : "Unknown error.", u);
        prog_a = 0;
        return;
    }
    page_len = 0; src_view = 0;
    loading = 1; load_t0 = now; prog = 0; prog_t = 220; prog_a = 1024;
}

static void push_history(const char *u)
{
    if (hist_n > 0 && hist_pos < hist_n) hist_sc[hist_pos] = sc_t;
    hist_n = hist_pos + 1;                              /* вперёд отбрасываем */
    if (hist_n >= HIST_N) {
        for (int i = 1; i < HIST_N; i++) { scpy(hist[i - 1], hist[i], sizeof hist[0]); hist_sc[i - 1] = hist_sc[i]; }
        hist_n = HIST_N - 1;
    }
    scpy(hist[hist_n], u, sizeof hist[0]); hist_sc[hist_n] = 0;
    hist_pos = hist_n++;
}

/* Ссылки результатов DuckDuckGo ведут через duckduckgo.com/l/?uddg=<адрес>: достаём настоящий адрес сразу. */
static void unwrap_ddg(char *u)
{
    const char *k = "duckduckgo.com/l/?";
    u32 n = slen(u);
    for (u32 i = 0; i + 18 < n; i++) {
        u32 j = 0; while (k[j] && u[i + j] == k[j]) j++;
        if (k[j]) continue;
        const char *q = u + i + 18;
        while (*q && !(q[0] == 'u' && q[1] == 'd' && q[2] == 'd' && q[3] == 'g' && q[4] == '=')) { while (*q && *q != '&') q++; if (*q == '&') q++; }
        if (!*q) return;
        q += 5;
        char out[URLMAX + 6]; u32 o = 0;
        while (*q && *q != '&' && o + 1 < sizeof out) {
            if (*q == '%' && q[1] && q[2]) {
                int h = (q[1] <= '9' ? q[1] - '0' : (q[1] | 32) - 'a' + 10) * 16 + (q[2] <= '9' ? q[2] - '0' : (q[2] | 32) - 'a' + 10);
                out[o++] = (char)h; q += 3;
            } else if (*q == '+') { out[o++] = ' '; q++; }
            else out[o++] = *q++;
        }
        out[o] = 0;
        if (starts(out, "http://") || starts(out, "https://")) scpy(u, out, URLMAX + 6);
        return;
    }
}

static void navigate(const char *typed)
{
    char u[URLMAX + 6];
    normalize(typed, u, sizeof u);
    if (!u[0]) return;
    push_history(u);
    start_load(u, 0);
}

static void go_back(void)
{
    if (hist_pos <= 0) return;
    hist_sc[hist_pos] = sc_t;
    hist_pos--;
    start_load(hist[hist_pos], hist_sc[hist_pos]);
}

static void go_forward(void)
{
    if (hist_pos + 1 >= hist_n) return;
    hist_sc[hist_pos] = sc_t;
    hist_pos++;
    start_load(hist[hist_pos], hist_sc[hist_pos]);
}

static void reload(void)
{
    if (loading) { net_fetch_abort(); loading = 0; prog_t = 0; return; }       /* кнопка превращается в "стоп" */
    if (url[0]) start_load(url, sc_t);
}

void browser_open(const char *u)
{
    if (!u || !*u) return;
    navigate(u);
}

void browser_init(void)
{
    hist_n = 0; hist_pos = 0; have_page = 0;
    navigate("endix://home");
    editing = 0;
}

const char *browser_title(void) { return have_page ? html_title() : ""; }

void browser_set_size(i32 w, i32 h)
{
    if (w != vw || h - TB != vh) { vw = w; vh = h - TB; if (vh < 40) vh = 40; lay_w = -1; }
}

/* ---------- прокрутка ---------- */

static i32 max_scroll(void) { i32 m = html_height() - vh; return m < 0 ? 0 : m; }

static void scroll_to(i32 t)
{
    i32 m = max_scroll();
    if (t < 0) t = 0;
    if (t > m) t = m;
    sc_t = t;
    sb_a_t = 1024;
    last_scroll_t = now;
}

static void ensure_layout(void)
{
    if (lay_w == vw || !have_page) return;
    html_layout(vw);
    lay_w = vw;
    if (restore_sc) { sc = sc_t = pending_sc; restore_sc = 0; }
    i32 m = max_scroll();
    if (sc_t > m) sc_t = m;
    if (sc > m) sc = m;
}

static void finish_load(void)
{
    net_fetch_t f; net_fetch_info(&f);
    loading = 0;
    prog_t = 1024;
    if (f.state == NF_ERROR) {
        char tmp[URLMAX + 6]; scpy(tmp, loading_url, sizeof tmp);
        show_error("Can't open the page", f.err[0] ? f.err : "Unknown error.", tmp);
        if (f.err[0] && starts(f.err, "This site redirects")) {
            /* предложим адрес с https:// как есть */
        }
        return;
    }
    if (!gen_wp) scpy(url, f.url, sizeof url);          /* адрес после перенаправлений */
    if (hist_n > 0 && !gen_wp) scpy(hist[hist_pos], url, sizeof hist[0]);
    if (!gen_wp && f.ctype == 2) { show_error("Can't display this file", "This is not a web page or text file (an image or another kind of file).", 0); return; }
    if (!gen_wp && f.status >= 400 && f.len < 30) {
        char m[40] = "The server answered with error ";
        u32 l = slen(m), v = (u32)f.status; m[l++] = (char)('0' + v / 100 % 10); m[l++] = (char)('0' + v / 10 % 10); m[l++] = (char)('0' + v % 10); m[l] = 0;
        show_error("The page is not available", m, url);
        return;
    }
    if (gen_wp) {
        gen_wp = 0; page_len = 0; src_view = 0;
        scpy(url, loading_url, sizeof url);
        if (f.status != 200 || f.truncated) { show_error("Wallpapers are not available", "The wallpaper service did not answer correctly (HTTP error or too long a reply).", loading_url); return; }
        pagebuf[f.len < PAGE_BUF ? f.len : PAGE_BUF - 1] = 0;
        wp_page_html((const char *)pagebuf, f.len, loading_url);
        return;
    }
    page_len = f.len; page_plain = f.ctype == 1; src_view = 0;
    show_html((const char *)pagebuf, f.len, url, page_plain);
}

/* ---------- рисование ---------- */

static u32 mix(u32 a, u32 b, i32 t)                     /* t: 0..255 */
{
    u32 r = ((a >> 16 & 255) * (255 - t) + (b >> 16 & 255) * t) / 255;
    u32 g = ((a >> 8 & 255) * (255 - t) + (b >> 8 & 255) * t) / 255;
    u32 bl = ((a & 255) * (255 - t) + (b & 255) * t) / 255;
    return r << 16 | g << 8 | bl;
}

#define PAGE_BG   page_bg_c
#define COL_TEXT  (page_dark ? RGB(230, 230, 232) : RGB(28, 28, 30))
#define COL_LINK  (page_dark ? RGB(125, 175, 255) : RGB(0, 94, 184))
#define COL_LINKH (page_dark ? RGB(180, 210, 255) : RGB(0, 60, 130))
#define COL_GRAY  (page_dark ? RGB(160, 163, 168) : RGB(120, 123, 128))


/* ---------- картинки ---------- */

static void img_reset(void)
{
    ipool_n = 0; iq_cur = -1; iq_next = 0;
    for (int i = 0; i < 32; i++) { idec[i].w = idec[i].h = 0; idec[i].a = 0; }
}

/* Качает картинки по одной после загрузки страницы; возвращает 1, если что-то изменилось. */
static int img_pump(void)
{
    int n = html_image_count();
    if (iq_cur >= 0) {
        net_fetch_t f; net_fetch_info(&f);
        if (f.state == NF_LOADING) return 0;
        int id = iq_cur, ok = 0, w = 0, h = 0; iq_cur = -1;
        if (f.state == NF_DONE && f.status < 400 && !f.truncated && f.len > 8 && img_info(imgbuf, f.len, &w, &h)) {
            int tw = w, th = h;
            if (tw > 800) { th = th * 800 / tw; tw = 800; }
            if (th > 600) { tw = tw * 600 / th; th = 600; }
            if (tw < 1) tw = 1;
            if (th < 1) th = 1;
            if (ipool_n + (u32)tw * (u32)th <= IPOOL && img_decode(imgbuf, f.len, tw, th, ipool + ipool_n)) {
                idec[id].off = ipool_n; idec[id].w = (i16)tw; idec[id].h = (i16)th; idec[id].a = 0;
                ipool_n += (u32)tw * (u32)th; ok = 1;
            }
        }
        html_image_set(id, ok ? w : 0, ok ? h : 0);
        lay_w = -1;
        return 1;
    }
    if (iq_next < n && !net_job_active()) {
        int id = iq_next++;
        if (net_fetch(html_image_url(id), imgbuf, IMG_BUF) < 0) html_image_set(id, 0, 0), lay_w = -1;
        else iq_cur = id;
        return 1;
    }
    return 0;
}

static void draw_img(i32 x, i32 y, const hbox_t *b, i32 x0, i32 x1, i32 y0, i32 y1)
{
    int id = b->color, st = html_image_state(id);
    if (st == 1 && idec[id].w > 0) {
        i32 dw = idec[id].w, dh = idec[id].h;
        const u32 *src = ipool + idec[id].off;
        u32 *fbp = fb_buffer(); i32 W = (i32)fb_width(), H = (i32)fb_height();
        i32 a = idec[id].a, t = a * 255 / 1024;
        i32 ya = y > y0 ? y : y0, yb = y + b->h < y1 ? y + b->h : y1; if (ya < 0) ya = 0; if (yb > H) yb = H;
        i32 xa = x > x0 ? x : x0, xb = x + b->w < x1 ? x + b->w : x1; if (xa < 0) xa = 0; if (xb > W) xb = W;
        for (i32 ry = ya; ry < yb; ry++) {
            const u32 *row = src + (u32)((ry - y) * dh / b->h) * (u32)dw;
            u32 *o = fbp + (u32)ry * (u32)W;
            for (i32 rx = xa; rx < xb; rx++) {
                u32 p = row[(rx - x) * dw / b->w];
                o[rx] = t >= 255 ? p : mix(PAGE_BG, p, t);
            }
        }
    } else if (st == 0) clipped_rect(x, y, b->w, b->h, y0, y1, RGB(240, 241, 243));
    else {
        clipped_rect(x, y, b->w, b->h, y0, y1, RGB(244, 245, 247));
        char tmp[48]; u32 l = b->len < 46 ? b->len : 46;
        for (u32 k = 0; k < l; k++) tmp[k] = b->text[k];
        tmp[l] = 0;
        if (l) font_draw(x + 8, y + 3, tmp, COL_GRAY, FONT_REGULAR);
    }
}

static void draw_reload(i32 cx, i32 cy, u32 ink, u32 bg)
{
    fb_fill_circle(cx, cy, 7, ink);
    fb_fill_circle(cx, cy, 5, bg);
    fb_fill_rect(cx + 1, cy - 8, 8, 7, bg);               /* разрыв кольца вверху справа */
    for (i32 k = 0; k < 4; k++) fb_fill_rect(cx + 1 + k, cy - 8 + k, 5 - 2 * k > 0 ? 5 - 2 * k : 1, 1, ink);   /* стрелка */
    fb_fill_rect(cx + 1, cy - 8, 1, 4, ink);
}

static void draw_stop(i32 cx, i32 cy, u32 ink)
{
    for (i32 k = -5; k <= 5; k++) {
        fb_fill_rect(cx + k, cy + k, 2, 2, ink);
        fb_fill_rect(cx + k, cy - k, 2, 2, ink);
    }
}

static void clipped_rect(i32 x, i32 y, i32 w, i32 h, i32 y0, i32 y1, u32 c)
{
    if (y < y0) { h -= y0 - y; y = y0; }
    if (y + h > y1) h = y1 - y;
    if (h > 0 && w > 0) fb_fill_rect(x, y, w, h, c);
}

void browser_draw(i32 x, i32 y, int focused)
{
    (void)focused;
    ensure_layout();
    const theme_t *th = theme();
    i32 py = y + TB, pb = py + vh;

    /* страница */
    fb_fill_rect(x, py, vw, vh, PAGE_BG);
    if (home_mode) home_draw(x, py);
    else if (have_page) {
        font_set_clip(py, pb);
        i32 top = sc, n = html_box_count();
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < n; i++) {
                const hbox_t *b = html_box(i);
                i32 by = py + b->y - top;
                if (by > pb || by + b->h + 4 < py) continue;
                if (pass == 0) {
                    if (b->kind == BK_BG && (b->rgb >> 24)) clipped_rect(x + b->x, by, b->w, b->h, py, pb, b->rgb & 0xFFFFFF);
                    else if (b->kind == BK_BG) {
                        if (by >= py && by + b->h <= pb) fb_fill_round_rect(x + b->x, by, b->w, b->h, 6, RGB(244, 245, 247), 255);
                        else clipped_rect(x + b->x, by, b->w, b->h, py, pb, RGB(244, 245, 247));
                    } else if (b->kind == BK_BAR) clipped_rect(x + b->x, by, b->w, b->h, py, pb, RGB(200, 203, 208));
                    else if (b->kind == BK_HR) clipped_rect(x + b->x, by, b->w, 1, py, pb, RGB(214, 216, 220));
                    continue;
                }
                if (b->kind == BK_DOT) { if (by >= py && by + 6 <= pb) fb_fill_circle(x + b->x + 3, by + 3, 3, COL_TEXT); continue; }
                if (b->kind == BK_IMG) { draw_img(x + b->x, by, b, x, x + vw, py, pb); continue; }
                if (b->kind != BK_TEXT) continue;
                char tmp[170]; u32 l = b->len < sizeof(tmp) - 1 ? b->len : sizeof(tmp) - 1;
                for (u32 k = 0; k < l; k++) tmp[k] = b->text[k];
                tmp[l] = 0;
                u32 col = (b->rgb >> 24) ? (b->rgb & 0xFFFFFF) : b->color == HC_LINK ? (b->link == link_hover ? COL_LINKH : COL_LINK) : b->color == HC_GRAY ? COL_GRAY : COL_TEXT;
                font_draw(x + b->x, by, tmp, col, b->font);
                if (b->uline) clipped_rect(x + b->x, by + b->h, b->w, 1, py, pb, col);
            }
        }
        font_clear_clip();
    } else {
        i32 w = font_width("Loading", FONT_BOLD);
        font_draw(x + (vw - w) / 2, py + vh / 2 - 8, "Loading", COL_GRAY, FONT_BOLD);
    }
    if (fade > 0) fb_blend_rect(x, py, vw, vh, PAGE_BG, (u32)(fade * 255 / 1024));

    /* полоса прокрутки (как в macOS: тонкая, появляется при прокрутке) */
    i32 m = max_scroll();
    if (m > 0 && sb_a > 0) {
        i32 th_h = vh * vh / html_height();
        if (th_h < 36) th_h = 36;
        i32 ty = py + 2 + (vh - 4 - th_h) * sc / m;
        i32 wide = sb_drag ? 9 : 7;
        fb_fill_round_rect(x + vw - wide - 3, ty, wide, th_h, wide / 2, RGB(90, 92, 98), (u32)(sb_a * 150 / 1024));
    }

    /* подсказка ссылки */
    if (st_a > 0 && status_txt[0]) {
        i32 w = font_width(status_txt, FONT_REGULAR);
        if (w > vw - 60) w = vw - 60;
        i32 sx = x + 10, sy = pb - 30;
        fb_fill_round_rect(sx, sy, w + 20, 24, 12, RGB(248, 248, 250), (u32)(st_a * 235 / 1024));
        font_set_clip(sy, sy + 24);
        font_draw(sx + 10, sy + 4, status_txt, mix(RGB(255, 255, 255), RGB(40, 40, 44), (st_a * 255) / 1024), FONT_REGULAR);
        font_clear_clip();
    }

    if (toast_a > 0 && toast[0]) {                                  /* плашка: скачивание и результат */
        i32 w = font_width(toast, FONT_BOLD) + 32; if (w > vw - 20) w = vw - 20;
        i32 sx = x + (vw - w) / 2, sy = pb - 52;
        fb_fill_round_rect(sx, sy, w, 36, 18, RGB(40, 40, 44), (u32)(toast_a * 225 / 1024));
        font_set_clip(sy, sy + 36);
        font_draw(sx + 16, sy + 9, toast, RGB(255, 255, 255), FONT_BOLD);
        font_clear_clip();
    }

    /* панель инструментов */
    u32 bar = th->body;
    fb_fill_rect(x, y, vw, TB, bar);
    fb_fill_rect(x, y + TB - 1, vw, 1, mix(bar, theme_is_dark() ? RGB(0, 0, 0) : RGB(120, 120, 125), 70));
    u32 ink = th->text, dim = th->sub;
    static const i32 bx[3] = { 12, 44, 76 };
    for (int i = 0; i < 3; i++) {
        i32 a = hv[i] * 40 / 1024;
        int enabled = i == 0 ? hist_pos > 0 : i == 1 ? hist_pos + 1 < hist_n : 1;
        if (a > 0 && enabled) fb_fill_round_rect(x + bx[i], y + 9, 28, 28, 8, theme_is_dark() ? RGB(255, 255, 255) : RGB(0, 0, 0), (u32)(a));
        u32 c = enabled ? ink : mix(bar, ink, 70);
        if (i == 0) ui_chevron(x + bx[i] + 13, y + 23, -1, c);
        else if (i == 1) ui_chevron(x + bx[i] + 15, y + 23, 1, c);
        else if (loading) draw_stop(x + bx[i] + 14, y + 23, c);
        else draw_reload(x + bx[i] + 14, y + 24, c, bar);
    }
    i32 ax = x + 114, aw = vw - 114 - 12;
    if (aw < 80) aw = 80;
    u32 pill = th->card;
    if (editing) fb_fill_round_rect(ax - 2, y + 6, aw + 4, 34, 11, theme_accent(), 110);      /* кольцо фокуса */
    fb_fill_round_rect(ax, y + 8, aw, 30, 9, theme_is_dark() ? mix(pill, RGB(255, 255, 255), 14 + hv[3] * 10 / 1024) : mix(RGB(236, 236, 240), RGB(226, 226, 232), hv[3] * 255 / 1024), 255);
    font_set_clip(y + 8, y + 38);
    if (editing) {
        i32 tx = ax + 12;
        if (sel_all && ed_n > 0) {
            i32 sw = font_width(ed, FONT_REGULAR);
            fb_fill_round_rect(tx - 2, y + 12, sw + 4, 22, 4, theme_accent(), 90);
        }
        font_draw(tx, y + 14, ed, ink, FONT_REGULAR);
        char save = ed[ed_pos]; ed[ed_pos] = 0;
        i32 cxp = tx + font_width(ed, FONT_REGULAR); ed[ed_pos] = save;
        if (((now / 50) & 1) == 0 || sel_all) { if (!sel_all) fb_fill_rect(cxp, y + 13, 1, 20, theme_accent()); }
    } else {
        char show[URLMAX + 6];
        const char *u = url;
        if (starts(u, "http://")) u += 7;
        scpy(show, u, sizeof show);
        if (starts(show, "endix://")) scpy(show, show + 8, sizeof show);
        i32 w = font_width(show, FONT_REGULAR);
        while (w > aw - 28 && slen(show) > 4) { show[slen(show) - 1] = 0; w = font_width(show, FONT_REGULAR); }
        if (w > aw - 28) w = aw - 28;
        font_draw(ax + (aw - w) / 2, y + 14, show, ink, FONT_REGULAR);
        if (loading) {                                      /* точки "идёт загрузка" справа в строке */
            for (int i = 0; i < 3; i++) {
                i32 ph = (i32)((now / 12 + (u32)i * 8) % 32);
                i32 a = ph < 16 ? ph * 16 : (32 - ph) * 16;
                fb_fill_circle(ax + aw - 16 + i * 0, y + 23, 0, dim);
                fb_blend_rect(ax + aw - 34 + i * 8, y + 22, 4, 4, ink, (u32)(60 + a / 2));
            }
        }
    }
    font_clear_clip();
    /* полоса загрузки под панелью */
    if (prog_a > 0 && prog > 0) {
        i32 w = vw * prog / 1024;
        fb_fill_rect(x, y + TB - 2, w, 2, theme_accent());
        if (prog_a < 1024) fb_blend_rect(x, y + TB - 2, w, 2, bar, (u32)(255 - prog_a * 255 / 1024));
    }
}

/* ---------- ввод ---------- */

static void begin_edit(void)
{
    editing = 1;
    sel_all = 1;
    scpy(ed, url, sizeof ed);
    ed_n = (int)slen(ed); ed_pos = ed_n;
}

static void ed_insert(int cp)
{
    char b[3]; int k = utf8_put(b, cp);
    if (sel_all) { ed_n = 0; ed_pos = 0; ed[0] = 0; sel_all = 0; }
    if (ed_n + k > URLMAX) return;
    for (int i = ed_n; i >= ed_pos; i--) ed[i + k] = ed[i];
    for (int i = 0; i < k; i++) ed[ed_pos + i] = b[i];
    ed_pos += k; ed_n += k;
}

static int in_scrollbar(i32 rx, i32 ry)
{
    return max_scroll() > 0 && rx >= vw - 16 && ry >= TB;
}

int browser_click(i32 rx, i32 ry, u32 ticks)
{
    now = ticks;
    if (ry < TB) {
        if (ry >= 9 && ry < 37) {
            if (rx >= 12 && rx < 40) { editing = 0; go_back(); return 1; }
            if (rx >= 44 && rx < 72) { editing = 0; go_forward(); return 1; }
            if (rx >= 76 && rx < 104) { editing = 0; reload(); return 1; }
        }
        if (rx >= 114 && ry >= 8 && ry < 38) { if (!editing) begin_edit(); else sel_all = 0; return 1; }
        return 0;
    }
    int was = editing;
    editing = 0;
    if (in_scrollbar(rx, ry)) {
        i32 m = max_scroll(), th_h = vh * vh / html_height();
        if (th_h < 36) th_h = 36;
        i32 ty = 2 + (vh - 4 - th_h) * sc / m;
        i32 yy = ry - TB;
        if (yy >= ty && yy < ty + th_h) sb_grab = yy - ty;
        else { sb_grab = th_h / 2; scroll_to((yy - sb_grab - 2) * m / (vh - 4 - th_h > 0 ? vh - 4 - th_h : 1)); }
        sb_drag = 1;
        return 1;
    }
    if (home_mode) {
        i32 gx, gy, c, sx, sy, sw; home_geom(&gx, &gy, &c, &sx, &sy, &sw);
        if (rx >= sx && rx < sx + sw && ry - TB >= sy && ry - TB < sy + 40) { begin_edit(); ed[0] = 0; ed_n = ed_pos = 0; sel_all = 0; return 1; }
        int t = home_tile_at(rx, ry);
        if (t >= 0) { navigate(tiles[t].url); return 1; }
        return was;
    }
    int l = html_link_at(rx, ry - TB + sc);
    if (l >= 0) {
        const char *u = html_link_url(l);
        if (u && starts(u, "endix://dl?")) { start_dl(u); return 1; }
        if (u) { char tmp[URLMAX + 6]; scpy(tmp, u, sizeof tmp); unwrap_ddg(tmp); navigate(tmp); return 1; }
    }
    return was;
}

int browser_mouse(i32 rx, i32 ry, int btn, int over)
{
    int dirty = 0;
    if (!(btn & 1) && sb_drag) { sb_drag = 0; dirty = 1; }
    if (sb_drag) {
        i32 m = max_scroll(), th_h = vh * vh / html_height();
        if (th_h < 36) th_h = 36;
        i32 den = vh - 4 - th_h > 0 ? vh - 4 - th_h : 1;
        i32 t = (ry - TB - sb_grab - 2) * m / den;
        scroll_to(t); sc = sc_t;
        return 1;
    }
    i32 nt[4] = { 0, 0, 0, 0 };
    int nl = -1;
    if (over) {
        if (ry >= 9 && ry < 37) {
            if (rx >= 12 && rx < 40) nt[0] = 1024;
            else if (rx >= 44 && rx < 72) nt[1] = 1024;
            else if (rx >= 76 && rx < 104) nt[2] = 1024;
        }
        if (ry >= 8 && ry < 38 && rx >= 114) nt[3] = 1024;
        if (ry >= TB && !in_scrollbar(rx, ry)) nl = home_mode ? -1 : html_link_at(rx, ry - TB + sc);
        if (in_scrollbar(rx, ry)) sb_a_t = 1024, last_scroll_t = now;
    }
    for (int i = 0; i < 4; i++) hv_t[i] = nt[i];
    if (home_mode) { int ht = over && ry >= TB ? home_tile_at(rx, ry) : -1; if (ht != home_hover) { home_hover = ht; dirty = 1; } }
    if (nl != link_hover) {
        link_hover = nl;
        if (nl >= 0) { const char *u = html_link_url(nl); scpy(status_txt, u ? u : "", sizeof status_txt); }
        dirty = 1;
    }
    return dirty;
}

int browser_key(i32 c, i32 ctrl)
{
    if (editing) {
        if (c == '\n') { editing = 0; char t[URLMAX + 6]; scpy(t, ed, sizeof t); navigate(t); return 1; }
        if (c == 27) { editing = 0; return 1; }
        if (c == '\b') {
            if (sel_all) { ed_n = 0; ed_pos = 0; ed[0] = 0; sel_all = 0; }
            else if (ed_pos > 0) { int k = ed_pos - utf8_prev(ed, ed_pos); for (int i = ed_pos; i <= ed_n; i++) ed[i - k] = ed[i]; ed_pos -= k; ed_n -= k; }
            return 1;
        }
        if (c == KEY_DELETE) { if (ed_pos < ed_n) { int k = utf8_clen(&ed[ed_pos]); for (int i = ed_pos; i + k <= ed_n; i++) ed[i] = ed[i + k]; ed_n -= k; } sel_all = 0; return 1; }
        if (c == KEY_LEFT) { if (sel_all) { sel_all = 0; ed_pos = 0; } else if (ed_pos > 0) ed_pos = utf8_prev(ed, ed_pos); return 1; }
        if (c == KEY_RIGHT) { if (sel_all) { sel_all = 0; ed_pos = ed_n; } else if (ed_pos < ed_n) ed_pos += utf8_clen(&ed[ed_pos]); return 1; }
        if (c == KEY_HOME) { sel_all = 0; ed_pos = 0; return 1; }
        if (c == KEY_END) { sel_all = 0; ed_pos = ed_n; return 1; }
        if (ctrl && (c == 'a' || c == 'A')) { sel_all = 1; return 1; }
        if (ctrl && (c == 'u' || c == 'U')) { ed_n = 0; ed_pos = 0; ed[0] = 0; sel_all = 0; return 1; }
        if (!ctrl && is_text_cp(c)) { ed_insert(c); return 1; }
        return 0;
    }
    if (ctrl) {
        if (c == 'l' || c == 'L' || c == 'k' || c == 'K') { begin_edit(); return 1; }
        if (c == 'r' || c == 'R') { reload(); return 1; }
        if ((c == 'u' || c == 'U') && page_len) {              /* исходный текст страницы */
            src_view = !src_view;
            show_html((const char *)pagebuf, page_len, url, src_view ? 1 : page_plain);
            return 1;
        }
        return 0;
    }
    switch (c) {
    case KEY_UP: scroll_to(sc_t - 48); return 1;
    case KEY_DOWN: scroll_to(sc_t + 48); return 1;
    case KEY_PGUP: scroll_to(sc_t - (vh - 48)); return 1;
    case KEY_PGDN: case ' ': scroll_to(sc_t + (vh - 48)); return 1;
    case KEY_HOME: scroll_to(0); return 1;
    case KEY_END: scroll_to(max_scroll()); return 1;
    case '\b': go_back(); return 1;
    case KEY_LEFT: go_back(); return 1;
    case KEY_RIGHT: go_forward(); return 1;
    case '/': begin_edit(); return 1;
    default: break;
    }
    return 0;
}

int browser_wheel(i32 d)
{
    scroll_to(sc_t - d * 54);
    return 1;
}

/* ---------- время ---------- */

int browser_tick(u32 ticks)
{
    now = ticks;
    int dirty = 0;
    if (loading) {
        net_fetch_t f; net_fetch_info(&f);
        if (f.state == NF_LOADING) {
            i32 t = 220 + (i32)(f.len / 64);
            if (t > 880) t = 880;
            i32 slow = 220 + (i32)((ticks - load_t0) * 3);          /* ползёт, даже пока тело не пришло */
            if (slow > 600) slow = 600;
            prog_t = t > slow ? t : slow;
            dirty = 1;
        } else { finish_load(); dirty = 1; }
    }
    if (dl_active) {
        net_fetch_t f; net_fetch_info(&f);
        if (f.state == NF_LOADING) {
            char m[48] = "Downloading... "; u32 l = slen(m), kb = f.len / 1024;
            if (kb >= 1024) { l = slen(m); m[l++] = (char)('0' + kb / 1024 % 10); m[l++] = '.'; m[l++] = (char)('0' + kb % 1024 * 10 / 1024); m[l++] = ' '; m[l++] = 'M'; m[l++] = 'B'; }
            else { if (kb >= 100) m[l++] = (char)('0' + kb / 100 % 10); if (kb >= 10) m[l++] = (char)('0' + kb / 10 % 10); m[l++] = (char)('0' + kb % 10); m[l++] = ' '; m[l++] = 'K'; m[l++] = 'B'; }
            m[l] = 0; scpy(toast, m, sizeof toast); toast_t = ticks; toast_a = 1024;
            if (ticks - dl_t0 > 12000) { net_fetch_abort(); dl_active = 0; set_toast("The download stalled."); }
        } else {
            dl_active = 0;
            if (f.state == NF_DONE && f.status == 200 && !f.truncated && f.len > 8) dl_finish(f.len);
            else set_toast(f.state == NF_ERROR && f.err[0] ? f.err : f.truncated ? "The picture is too large to download." : "The server refused the download.");
        }
        dirty = 1;
    }
    if (have_page && !loading && !dl_active && img_pump()) dirty = 1;
    if (toast[0] && !dl_active && ticks - toast_t > 400) {
        toast_a = anim_approach(toast_a, 0, 8); dirty = 1;
        if (toast_a <= 0) { toast_a = 0; toast[0] = 0; }
    }
    u32 dt = ticks - last_anim;
    if (dt == 0) return dirty;
    if (dt > 8) dt = 8;
    last_anim = ticks;
    i32 p0 = prog, a0 = prog_a, f0 = fade, s0 = sc, sb0 = sb_a, st0 = st_a, h0[4];
    for (int i = 0; i < 4; i++) h0[i] = hv[i];
    prog = anim_approach(prog, prog_t, dt);
    if (!loading && prog >= 1000) prog_a = anim_approach(prog_a, 0, dt);
    if (!loading && prog_a == 0) { prog = 0; prog_t = 0; }
    if (fade > 0) fade = anim_approach(fade, 0, dt);
    for (int i = 0; i < 32; i++) if (idec[i].w > 0 && idec[i].a < 1024) { idec[i].a = anim_approach(idec[i].a, 1024, dt); dirty = 1; }
    if (sc != sc_t) {
        i32 d = sc_t - sc, step = d * 30 / 100;
        for (u32 k = 1; k < dt; k++) step += (d - step) * 20 / 100;
        if (step == 0) step = d > 0 ? 1 : -1;
        if ((d > 0 && step > d) || (d < 0 && step < d)) step = d;
        sc += step;
    }
    if (sb_drag || (ticks - last_scroll_t) < 80) sb_a_t = 1024; else if (!sb_drag) sb_a_t = 0;
    sb_a = anim_approach(sb_a, sb_a_t, dt);
    st_a = anim_approach(st_a, link_hover >= 0 ? 1024 : 0, dt);
    for (int i = 0; i < 4; i++) hv[i] = anim_approach(hv[i], hv_t[i], dt);
    if (prog != p0 || prog_a != a0 || fade != f0 || sc != s0 || sb_a != sb0 || st_a != st0) dirty = 1;
    for (int i = 0; i < 4; i++) if (hv[i] != h0[i]) dirty = 1;
    if (editing && ((ticks / 50) != ((ticks - dt) / 50))) dirty = 1;      /* мигание курсора */
    return dirty;
}
