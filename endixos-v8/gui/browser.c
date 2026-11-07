/* Браузер EndixOS: окно с адресной строкой и страницей. Страница грузится из сети (kernel/net.c)
 * или берётся из встроенных адресов endix://. Всё движется плавно: прогресс, прокрутка, полоса, подсказка ссылки. */
#include "browser.h"
#include "html.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "ui.h"
#include "anim.h"
#include "img.h"
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
static struct { u32 off; i16 w, h; i32 a; } idec[16];
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

/* ---------- встроенные страницы ---------- */

static const char HOME_HTML[] =
    "<title>Start Page</title>"
    "<center><h1>EndixOS Browser</h1>"
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
    "<title>About EndixOS Browser</title>"
    "<h1>About</h1>"
    "<p>This browser is part of EndixOS 8.0. Everything underneath is written from scratch:</p>"
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
    if (seq(u, "endix://home") || seq(u, "endix://")) { show_html(HOME_HTML, sizeof(HOME_HTML) - 1, "endix://home", 0); prog = 0; prog_a = 0; return; }
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
    scpy(url, f.url, sizeof url);                       /* адрес после перенаправлений */
    if (hist_n > 0) scpy(hist[hist_pos], url, sizeof hist[0]);
    if (f.ctype == 2) { show_error("Can't display this file", "This is not a web page or text file (an image or another kind of file).", 0); return; }
    if (f.status >= 400 && f.len < 30) {
        char m[40] = "The server answered with error ";
        u32 l = slen(m), v = (u32)f.status; m[l++] = (char)('0' + v / 100 % 10); m[l++] = (char)('0' + v / 10 % 10); m[l++] = (char)('0' + v % 10); m[l] = 0;
        show_error("The page is not available", m, url);
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
    for (int i = 0; i < 16; i++) { idec[i].w = idec[i].h = 0; idec[i].a = 0; }
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
    if (have_page) {
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

static void ed_insert(char c)
{
    if (sel_all) { ed_n = 0; ed_pos = 0; ed[0] = 0; sel_all = 0; }
    if (ed_n >= URLMAX) return;
    for (int i = ed_n; i >= ed_pos; i--) ed[i + 1] = ed[i];
    ed[ed_pos++] = c; ed_n++;
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
    int l = html_link_at(rx, ry - TB + sc);
    if (l >= 0) {
        const char *u = html_link_url(l);
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
        if (ry >= TB && !in_scrollbar(rx, ry)) nl = html_link_at(rx, ry - TB + sc);
        if (in_scrollbar(rx, ry)) sb_a_t = 1024, last_scroll_t = now;
    }
    for (int i = 0; i < 4; i++) hv_t[i] = nt[i];
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
            else if (ed_pos > 0) { for (int i = ed_pos; i <= ed_n; i++) ed[i - 1] = ed[i]; ed_pos--; ed_n--; }
            return 1;
        }
        if (c == KEY_DELETE) { if (ed_pos < ed_n) { for (int i = ed_pos; i < ed_n; i++) ed[i] = ed[i + 1]; ed_n--; } sel_all = 0; return 1; }
        if (c == KEY_LEFT) { if (sel_all) { sel_all = 0; ed_pos = 0; } else if (ed_pos > 0) ed_pos--; return 1; }
        if (c == KEY_RIGHT) { if (sel_all) { sel_all = 0; ed_pos = ed_n; } else if (ed_pos < ed_n) ed_pos++; return 1; }
        if (c == KEY_HOME) { sel_all = 0; ed_pos = 0; return 1; }
        if (c == KEY_END) { sel_all = 0; ed_pos = ed_n; return 1; }
        if (ctrl && (c == 'a' || c == 'A')) { sel_all = 1; return 1; }
        if (ctrl && (c == 'u' || c == 'U')) { ed_n = 0; ed_pos = 0; ed[0] = 0; sel_all = 0; return 1; }
        if (!ctrl && c >= 32 && c < 127) { ed_insert((char)c); return 1; }
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
    if (have_page && !loading && img_pump()) dirty = 1;
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
    for (int i = 0; i < 16; i++) if (idec[i].w > 0 && idec[i].a < 1024) { idec[i].a = anim_approach(idec[i].a, 1024, dt); dirty = 1; }
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
