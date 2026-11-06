/* Простой HTML: разбор в поток узлов и вёрстка в прямоугольники. Только статические массивы. */
#include "html.h"
#include "font.h"
#include "../kernel/net.h"

#define MAXNODES 9000
#define MAXBOXES 12000
#define POOLSZ   (160 * 1024)
#define MAXLINKS 400
#define MAXSTACK 32
#define MARGIN   22

enum { N_WORD, N_BR, N_PARA, N_HR, N_INDENT, N_BULLET, N_ALIGN, N_PRE, N_QUOTE, N_GAP, N_IMG, N_TBL, N_TR, N_TD, N_TEND, N_BGO, N_BGC };

#define MAXIMG 16
typedef struct { u32 url, alt; u16 aw, ah; i16 nw, nh; u8 state; } img_t;      /* state: 0 ждёт, 1 готова, 2 не вышло */
static img_t imgs[MAXIMG]; static int nimg;
enum { F_SP = 1, F_ULINE = 2 };

typedef struct { u32 off; u16 w; u8 type, font, flags, color; i16 link; i16 aux; u32 rgb; } node_t;

static node_t nodes[MAXNODES]; static int nn;
static char pool[POOLSZ]; static u32 pn;
static const char *links[MAXLINKS]; static int nlinks;
static char title[100];
static hbox_t boxes[MAXBOXES]; static int nb;
static i32 page_h;
static int trunc;
static char base[256];

static int strcmp_(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

/* ---------- символы и сущности ---------- */

static int in_font(u32 cp) { return (cp >= 0x20 && cp <= 0x7E) || (cp >= 0x410 && cp <= 0x44F) || cp == 0x401 || cp == 0x451; }

/* Дописывает символ в буфер (UTF-8, только то, что есть в шрифте); возвращает новую длину. */
static u32 put_cp(char *b, u32 n, u32 cap, u32 cp)
{
    const char *rep;
    if (cp < 0x20) return n;
    if (in_font(cp)) {
        if (cp < 0x80) { if (n + 1 < cap) b[n++] = (char)cp; }
        else if (n + 2 < cap) { b[n++] = (char)(0xC0 | cp >> 6); b[n++] = (char)(0x80 | (cp & 0x3F)); }
        return n;
    }
    switch (cp) {
    case 0xA0: rep = " "; break;
    case 0x2013: case 0x2014: case 0x2212: rep = "-"; break;
    case 0x2018: case 0x2019: case 0x201A: rep = "'"; break;
    case 0x201C: case 0x201D: case 0x201E: rep = "\""; break;
    case 0x2026: rep = "..."; break;
    case 0x2022: case 0xB7: rep = "*"; break;
    case 0xA9: rep = "(c)"; break;
    case 0xAE: rep = "(R)"; break;
    case 0x2122: rep = "(TM)"; break;
    case 0xAB: rep = "<<"; break;
    case 0xBB: rep = ">>"; break;
    case 0x2192: rep = "->"; break;
    case 0x2190: rep = "<-"; break;
    case 0xD7: rep = "x"; break;
    case 0x20AC: rep = "EUR"; break;
    case 0xB0: rep = "deg"; break;
    default: rep = "?"; break;
    }
    while (*rep && n + 1 < cap) b[n++] = *rep++;
    return n;
}

static const struct { const char *n; u32 cp; } ents[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
    { "ndash", 0x2013 }, { "mdash", 0x2014 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C },
    { "rdquo", 0x201D }, { "hellip", 0x2026 }, { "bull", 0x2022 }, { "middot", 0xB7 }, { "copy", 0xA9 },
    { "reg", 0xAE }, { "trade", 0x2122 }, { "laquo", 0xAB }, { "raquo", 0xBB }, { "rarr", 0x2192 },
    { "larr", 0x2190 }, { "times", 0xD7 }, { "euro", 0x20AC }, { "deg", 0xB0 }, { "minus", 0x2212 },
};

/* s указывает на '&'. Возвращает длину сущности (0 - не сущность) и кодовую точку. */
static u32 entity(const char *s, u32 avail, u32 *cp)
{
    if (avail < 3) return 0;
    if (s[1] == '#') {
        u32 i = 2, v = 0;
        int hex = (s[2] | 32) == 'x';
        if (hex) i++;
        u32 d0 = i;
        while (i < avail && i < d0 + 8) {
            char c = s[i];
            if (c >= '0' && c <= '9') v = v * (hex ? 16 : 10) + (u32)(c - '0');
            else if (hex && (c | 32) >= 'a' && (c | 32) <= 'f') v = v * 16 + (u32)((c | 32) - 'a' + 10);
            else break;
            i++;
        }
        if (i == d0) return 0;
        if (i < avail && s[i] == ';') i++;
        *cp = v;
        return i;
    }
    for (u32 k = 0; k < sizeof(ents) / sizeof(ents[0]); k++) {
        u32 j = 0;
        while (ents[k].n[j] && 1 + j < avail && s[1 + j] == ents[k].n[j]) j++;
        if (!ents[k].n[j] && 1 + j < avail && s[1 + j] == ';') { *cp = ents[k].cp; return j + 2; }
    }
    return 0;
}

/* ---------- состояние разбора ---------- */

typedef struct { u8 tag, font, color, uline, pre, center, ctr_own, nodisp, dark, has_rgb, bgo; u16 nh; i16 link; u32 rgb; } style_t;
static u16 cur_nh; static int css_hide;
static style_t st[MAXSTACK]; static int sp_;
static char word[200]; static u32 wl;
static int pend_space, hidden, in_title, ol_depth;
static int ol_n[8], list_ord[8];

enum { T_OTHER, T_A, T_B, T_P, T_DIV, T_H, T_UL, T_OL, T_LI, T_PRE, T_BQ, T_TABLE, T_TR, T_TD, T_TH, T_CENTER, T_HIDE,
       T_TITLE, T_CODE, T_DT, T_DD };

static u32 add_pool(const char *s, u32 n)
{
    if (pn + n + 1 > POOLSZ) { trunc = 1; return 0; }
    u32 o = pn;
    for (u32 i = 0; i < n; i++) pool[pn++] = s[i];
    pool[pn++] = 0;
    return o;
}

static node_t *add_node(int type)
{
    if (nn >= MAXNODES) { trunc = 1; return 0; }
    node_t *n = &nodes[nn++];
    n->off = 0; n->w = 0; n->type = (u8)type; n->font = 0; n->flags = 0; n->color = 0; n->link = -1; n->aux = 0; n->rgb = 0;
    return n;
}

static void flush_word(void)
{
    if (!wl) return;
    word[wl] = 0;
    style_t *s = &st[sp_];
    node_t *n = add_node(N_WORD);
    if (n) {
        n->off = add_pool(word, wl);
        n->font = s->font; n->color = s->color; n->link = s->link;
        n->flags = (u8)((pend_space ? F_SP : 0) | (s->uline ? F_ULINE : 0));
        n->w = (u16)font_width(word, s->font);
        n->rgb = s->has_rgb ? (s->rgb | 0x1000000u) : s->dark ? ((s->link >= 0 ? 0x8CB4FFu : 0xE6E6E6u) | 0x1000000u) : 0;
    }
    pend_space = 0;
    wl = 0;
}

static void emit_break(int type, int aux)
{
    flush_word();
    node_t *n = add_node(type);
    if (n) n->aux = (i16)aux;
    pend_space = 0;
}

static void emit_text(const char *s, u32 len)
{
    style_t *cur = &st[sp_];
    for (u32 i = 0; i < len;) {
        u8 c = (u8)s[i];
        u32 cp, e;
        if (c == '&' && (e = entity(s + i, len - i, &cp)) != 0) i += e;
        else if (c == '&') { cp = '&'; i++; }
        else if (c < 0x80) { cp = c; i++; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < len && ((u8)s[i+1] & 0xC0) == 0x80) { cp = (u32)(c & 0x1F) << 6 | ((u8)s[i+1] & 0x3F); i += 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < len && ((u8)s[i+1] & 0xC0) == 0x80) { cp = (u32)(c & 0x0F) << 12 | (u32)((u8)s[i+1] & 0x3F) << 6 | ((u8)s[i+2] & 0x3F); i += 3; }
        else { cp = '?'; i++; while (i < len && ((u8)s[i] & 0xC0) == 0x80) i++; }

        if (in_title) {
            u32 l = slen(title);
            if (cp == '\n' || cp == '\r' || cp == '\t') cp = ' ';
            if (cp == ' ' && (l == 0 || title[l - 1] == ' ')) continue;
            title[put_cp(title, l, sizeof(title), cp)] = 0;
            continue;
        }
        if (hidden || css_hide) continue;
        if (cur->pre) {
            if (cp == '\n') { flush_word(); node_t *n = add_node(N_BR); if (n) n->aux = 1; continue; }
            if (cp == '\r') continue;
            if (cp == '\t') { for (int k = 0; k < 4; k++) wl = put_cp(word, wl, sizeof(word), ' '); continue; }
            wl = put_cp(word, wl, sizeof(word), cp);
            if (wl > 150) flush_word();
            continue;
        }
        if (cp == ' ' || cp == '\n' || cp == '\t' || cp == '\r') { flush_word(); pend_space = 1; continue; }
        wl = put_cp(word, wl, sizeof(word), cp);
        if (wl > 150) { flush_word(); pend_space = 0; }
    }
}

/* ---------- теги ---------- */

static int tag_id(const char *t, int *heading)
{
    *heading = 0;
    #define IS(x) (!strcmp_(t, x))
    if (t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && !t[2]) { *heading = t[1] - '0'; return T_H; }
    if (IS("a")) return T_A;
    if (IS("b") || IS("strong") || IS("em") || IS("i") || IS("u") || IS("mark") || IS("cite") || IS("dfn")) return T_B;
    if (IS("p")) return T_P;
    if (IS("div") || IS("section") || IS("article") || IS("header") || IS("footer") || IS("main") || IS("nav") ||
        IS("aside") || IS("form") || IS("figure") || IS("figcaption") || IS("address") || IS("fieldset") || IS("body") || IS("html")) return T_DIV;
    if (IS("ul") || IS("menu") || IS("dir")) return T_UL;
    if (IS("ol")) return T_OL;
    if (IS("li")) return T_LI;
    if (IS("pre") || IS("textarea") || IS("listing") || IS("xmp")) return T_PRE;
    if (IS("blockquote")) return T_BQ;
    if (IS("table")) return T_TABLE;
    if (IS("tr")) return T_TR;
    if (IS("td")) return T_TD;
    if (IS("th")) return T_TH;
    if (IS("center")) return T_CENTER;
    if (IS("script") || IS("style") || IS("noscript") || IS("template") || IS("svg") || IS("head") || IS("iframe") ||
        IS("object") || IS("select") || IS("button") || IS("canvas") || IS("math")) return T_HIDE;
    if (IS("title")) return T_TITLE;
    if (IS("code") || IS("tt") || IS("kbd") || IS("samp") || IS("var")) return T_CODE;
    if (IS("dt")) return T_DT;
    if (IS("dd")) return T_DD;
    return T_OTHER;
    #undef IS
}

static int attr(const char *a, u32 alen, const char *name, char *out, u32 cap)
{
    u32 nl = slen(name), i = 0;
    while (i < alen) {
        while (i < alen && (a[i] == ' ' || a[i] == '\n' || a[i] == '\t' || a[i] == '\r' || a[i] == '/')) i++;
        u32 s = i;
        while (i < alen && a[i] != '=' && a[i] != ' ' && a[i] != '\n' && a[i] != '\t' && a[i] != '\r' && a[i] != '/') i++;
        u32 l = i - s;
        while (i < alen && (a[i] == ' ' || a[i] == '\n' || a[i] == '\t')) i++;
        u32 vs = 0, ve = 0;
        if (i < alen && a[i] == '=') {
            i++;
            while (i < alen && (a[i] == ' ' || a[i] == '\n' || a[i] == '\t')) i++;
            if (i < alen && (a[i] == '"' || a[i] == '\'')) {
                char q = a[i++]; vs = i;
                while (i < alen && a[i] != q) i++;
                ve = i; if (i < alen) i++;
            } else { vs = i; while (i < alen && a[i] != ' ' && a[i] != '\n' && a[i] != '\t' && a[i] != '\r') i++; ve = i; }
        }
        if (l == nl) {
            u32 k = 0; while (k < l && lower(a[s + k]) == name[k]) k++;
            if (k == l) {
                u32 n = 0;
                for (u32 z = vs; z < ve && n + 1 < cap; z++) out[n++] = a[z];
                out[n] = 0;
                return 1;
            }
        }
    }
    return 0;
}

static void push_style(int tag)
{
    if (sp_ + 1 >= MAXSTACK) return;
    st[sp_ + 1] = st[sp_]; sp_++;
    st[sp_].tag = (u8)tag; st[sp_].nh = cur_nh; st[sp_].ctr_own = 0; st[sp_].nodisp = 0; st[sp_].bgo = 0;
}

static int starts_(const char *s, const char *p) { while (*p && lower(*s) == *p) { s++; p++; } return !*p; }
static u32 num_(const char *s) { u32 v = 0; while (*s >= '0' && *s <= '9' && v < 9999) v = v * 10 + (u32)(*s++ - '0'); return v; }

/* ---------- простой CSS ---------- */

#define S_COLOR 1
#define S_BG 2
#define S_BOLD 4
#define S_NORMAL 8
#define S_CENTER 16
#define S_NODISP 32
#define S_NOUL 64
#define S_UL 128
#define S_BIG 256
#define S_WIDTH 512
typedef struct { u32 color, bg; u16 set; i16 wd; } css_t;
typedef struct { char tag[10], cls[20], id[20]; u8 spec; css_t d; } rule_t;
#define MAXRULES 240
static rule_t rules[MAXRULES]; static int nrules;
static u32 page_bg_rgb = 0xFFFFFF;

static int hexv(int c) { c = lower(c); return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
static i32 lum(u32 c) { return (i32)(((c >> 16 & 255) * 3 + (c >> 8 & 255) * 6 + (c & 255)) / 10); }

static int parse_color(const char *s, u32 *out)
{
    static const struct { const char *n; u32 c; } nm[] = { {"white",0xFFFFFF},{"black",0},{"red",0xFF0000},{"green",0x008000},{"blue",0x0000FF},{"gray",0x808080},{"grey",0x808080},
        {"orange",0xFFA500},{"yellow",0xFFFF00},{"purple",0x800080},{"pink",0xFFC0CB},{"brown",0xA52A2A},{"silver",0xC0C0C0},{"navy",0x000080},{"teal",0x008080},
        {"maroon",0x800000},{"lime",0x00FF00},{"aqua",0x00FFFF},{"cyan",0x00FFFF},{"magenta",0xFF00FF},{"fuchsia",0xFF00FF},{"olive",0x808000},
        {"darkgray",0xA9A9A9},{"lightgray",0xD3D3D3},{"lightgrey",0xD3D3D3},{"gold",0xFFD700},{"whitesmoke",0xF5F5F5},{"darkblue",0x00008B},{"darkred",0x8B0000} };
    while (*s == ' ') s++;
    if (*s == '#') {
        int d[6], n = 0; s++;
        while (n < 6 && hexv(s[n]) >= 0) { d[n] = hexv(s[n]); n++; }
        if (n == 3) { *out = (u32)(d[0] * 17) << 16 | (u32)(d[1] * 17) << 8 | (u32)(d[2] * 17); return 1; }
        if (n == 6) { *out = (u32)(d[0] * 16 + d[1]) << 16 | (u32)(d[2] * 16 + d[3]) << 8 | (u32)(d[4] * 16 + d[5]); return 1; }
        return 0;
    }
    if (starts_(s, "rgb")) {
        u32 v[3];
        while (*s && *s != '(') s++;
        if (!*s) return 0;
        s++;
        for (int i = 0; i < 3; i++) {
            while (*s == ' ' || *s == ',') s++;
            if (*s < '0' || *s > '9') return 0;
            v[i] = num_(s);
            while ((*s >= '0' && *s <= '9') || *s == '.') s++;
            if (*s == '%') { v[i] = v[i] * 255 / 100; s++; }
            if (v[i] > 255) v[i] = 255;
        }
        *out = v[0] << 16 | v[1] << 8 | v[2];
        return 1;
    }
    for (u32 k = 0; k < sizeof nm / sizeof nm[0]; k++) {
        const char *p = nm[k].n; int i = 0;
        while (p[i] && lower(s[i]) == p[i]) i++;
        if (!p[i] && !((lower(s[i]) >= 'a' && lower(s[i]) <= 'z') || s[i] == '-')) { *out = nm[k].c; return 1; }
    }
    return 0;
}

static int bg_scan(const char *v, u32 *out)
{
    while (*v) {
        while (*v == ' ') v++;
        if (parse_color(v, out)) return 1;
        int par = 0;
        while (*v && (*v != ' ' || par)) { if (*v == '(') par++; else if (*v == ')') par--; v++; }
    }
    return 0;
}

static void css_decls(const char *s, u32 n, css_t *d)
{
    d->set = 0; d->color = d->bg = 0; d->wd = 0;
    u32 i = 0;
    while (i < n) {
        while (i < n && (s[i] == ' ' || s[i] == ';' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) i++;
        char prop[24]; u32 pl = 0;
        while (i < n && s[i] != ':' && s[i] != ';') { if (pl < 23) prop[pl++] = (char)lower(s[i]); i++; }
        while (pl && (prop[pl - 1] == ' ' || prop[pl - 1] == '\n')) pl--;
        prop[pl] = 0;
        if (i >= n || s[i] != ':') continue;
        i++;
        char val[80]; u32 vl = 0; int par = 0;
        while (i < n && (s[i] != ';' || par)) {
            if (s[i] == '(') par++; else if (s[i] == ')' && par) par--;
            if (vl < 79) val[vl++] = (char)lower(s[i]);
            i++;
        }
        val[vl] = 0;
        const char *v = val; while (*v == ' ' || *v == '\n' || *v == '\t' || *v == '\r') v++;
        u32 c;
        if (!strcmp_(prop, "color")) { if (parse_color(v, &c)) { d->color = c; d->set |= S_COLOR; } }
        else if (!strcmp_(prop, "background-color") || !strcmp_(prop, "background")) { if (bg_scan(v, &c)) { d->bg = c; d->set |= S_BG; } }
        else if (!strcmp_(prop, "font-weight")) {
            if (starts_(v, "bold") || (v[0] >= '6' && v[0] <= '9')) { d->set |= S_BOLD; d->set &= (u16)~S_NORMAL; }
            else if (starts_(v, "normal") || starts_(v, "lighter") || (v[0] >= '1' && v[0] <= '5')) { d->set |= S_NORMAL; d->set &= (u16)~S_BOLD; }
        }
        else if (!strcmp_(prop, "text-align")) { if (starts_(v, "center")) d->set |= S_CENTER; }
        else if (!strcmp_(prop, "display")) { if (starts_(v, "none")) d->set |= S_NODISP; }
        else if (!strcmp_(prop, "visibility")) { if (starts_(v, "hidden")) d->set |= S_NODISP; }
        else if (!strcmp_(prop, "text-decoration")) {
            if (starts_(v, "none")) { d->set |= S_NOUL; d->set &= (u16)~S_UL; }
            else if (starts_(v, "underline")) { d->set |= S_UL; d->set &= (u16)~S_NOUL; }
        }
        else if (!strcmp_(prop, "font-size")) {
            u32 ip = num_(v); const char *p = v; while (*p >= '0' && *p <= '9') p++;
            u32 fr = 0; if (*p == '.') { p++; fr = (u32)(*p >= '0' && *p <= '9' ? *p - '0' : 0); while (*p >= '0' && *p <= '9') p++; }
            u32 x10 = ip * 10 + fr; int big = 0;
            if (starts_(p, "px")) big = x10 >= 240; else if (starts_(p, "em") || starts_(p, "rem")) big = x10 >= 16;
            else if (*p == '%') big = ip >= 160; else if (starts_(p, "pt")) big = x10 >= 180;
            else if (starts_(v, "x-large") || starts_(v, "xx-large") || starts_(v, "xxx-large")) big = 1;
            if (big) d->set |= S_BIG;
        }
        else if (!strcmp_(prop, "width")) {
            u32 ip = num_(v); const char *p = v; while (*p >= '0' && *p <= '9') p++;
            if (*p == '%' && ip > 0 && ip <= 100) { d->wd = (i16)-(i32)ip; d->set |= S_WIDTH; }
            else if (starts_(p, "px") && ip > 0) { d->wd = (i16)(ip > 3000 ? 3000 : ip); d->set |= S_WIDTH; }
        }
    }
}

static void css_merge(css_t *to, const css_t *f)
{
    if (f->set & S_COLOR) to->color = f->color;
    if (f->set & S_BG) to->bg = f->bg;
    if (f->set & S_WIDTH) to->wd = f->wd;
    if (f->set & S_BOLD) to->set &= (u16)~S_NORMAL;
    if (f->set & S_NORMAL) to->set &= (u16)~S_BOLD;
    if (f->set & S_NOUL) to->set &= (u16)~S_UL;
    if (f->set & S_UL) to->set &= (u16)~S_NOUL;
    to->set |= f->set;
}

static void add_rule(const char *s, u32 n, const css_t *d)
{
    while (n && (*s == ' ' || *s == '\n' || *s == '\t' || *s == '\r')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;
    if (!n || nrules >= MAXRULES) return;
    rule_t *r = &rules[nrules];
    r->tag[0] = r->cls[0] = r->id[0] = 0; r->spec = 0;
    u32 i = 0, k = 0;
    while (i < n && ((lower(s[i]) >= 'a' && lower(s[i]) <= 'z') || (s[i] >= '0' && s[i] <= '9'))) { if (k < 9) r->tag[k++] = (char)lower(s[i]); i++; }
    r->tag[k] = 0;
    if (i < n && (s[i] == '.' || s[i] == '#')) {
        char *dst = s[i] == '.' ? r->cls : r->id; r->spec = s[i] == '.' ? (k ? 2 : 1) : 3; i++; k = 0;
        while (i < n && ((lower(s[i]) >= 'a' && lower(s[i]) <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-' || s[i] == '_')) { if (k < 19) dst[k++] = s[i]; i++; }
        dst[k] = 0;
    }
    if (i != n || (!r->tag[0] && !r->cls[0] && !r->id[0])) return;       /* комбинаторы, псевдоклассы и т.п. не поддержаны */
    r->d = *d;
    nrules++;
}

static void css_sheet(const char *s, u32 n)
{
    u32 i = 0;
    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') { i++; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') { i += 2; while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++; i += 2; continue; }
        if (c == '<' && i + 3 < n && s[i + 1] == '!') { i += 4; continue; }
        if (c == '-' && i + 2 < n && s[i + 1] == '-' && s[i + 2] == '>') { i += 3; continue; }
        if (c == '@') {
            while (i < n && s[i] != '{' && s[i] != ';') i++;
            if (i < n && s[i] == '{') { int dd = 0; while (i < n) { if (s[i] == '{') dd++; else if (s[i] == '}' && --dd == 0) { i++; break; } i++; } }
            else i++;
            continue;
        }
        u32 ss = i; while (i < n && s[i] != '{') i++;
        u32 se = i; if (i >= n) return;
        i++;
        u32 bs = i; while (i < n && s[i] != '}') i++;
        u32 be = i; i++;
        css_t d; css_decls(s + bs, be - bs, &d);
        if (!d.set) continue;
        u32 p = ss;
        while (p <= se) { u32 q = p; while (q < se && s[q] != ',') q++; add_rule(s + p, q - p, &d); p = q + 1; }
    }
}

static int has_class(const char *list, const char *name)
{
    u32 nl = slen(name);
    while (*list) {
        while (*list == ' ') list++;
        u32 k = 0; while (list[k] && list[k] != ' ') k++;
        if (k == nl) { u32 z = 0; while (z < k && list[z] == name[z]) z++; if (z == k) return 1; }
        list += k;
    }
    return 0;
}

static void get_css(const char *t, const char *a, u32 alen, css_t *cs)
{
    char cls[80], idv[40], sty[400], v[40];
    u32 c;
    cs->set = 0; cs->color = cs->bg = 0; cs->wd = 0;
    cls[0] = idv[0] = sty[0] = 0;
    attr(a, alen, "class", cls, sizeof cls); attr(a, alen, "id", idv, sizeof idv); attr(a, alen, "style", sty, sizeof sty);
    for (int pass = 0; pass < 4 && nrules; pass++)
        for (int i = 0; i < nrules; i++) {
            const rule_t *r = &rules[i];
            if (r->spec != pass) continue;
            if (r->tag[0] && strcmp_(r->tag, t)) continue;
            if (r->cls[0] && !has_class(cls, r->cls)) continue;
            if (r->id[0] && strcmp_(r->id, idv)) continue;
            css_merge(cs, &r->d);
        }
    if (attr(a, alen, "bgcolor", v, sizeof v) && parse_color(v, &c) && !(cs->set & S_BG)) { cs->bg = c; cs->set |= S_BG; }
    if (!strcmp_(t, "font") && attr(a, alen, "color", v, sizeof v) && parse_color(v, &c)) { cs->color = c; cs->set |= S_COLOR; }
    if (!strcmp_(t, "table") && attr(a, alen, "width", v, sizeof v) && !(cs->set & S_WIDTH)) {
        u32 ip = num_(v); const char *p = v; while (*p >= '0' && *p <= '9') p++;
        if (ip > 0 && *p == '%' && ip <= 100) { cs->wd = (i16)-(i32)ip; cs->set |= S_WIDTH; }
        else if (ip > 0 && *p != '%') { cs->wd = (i16)(ip > 3000 ? 3000 : ip); cs->set |= S_WIDTH; }
    }
    if (sty[0]) { css_t d; css_decls(sty, slen(sty), &d); css_merge(cs, &d); }
}

static u16 nh_of(const char *t) { u32 h = 5381; while (*t) h = h * 33 + (u8)*t++; return (u16)h; }

static int link_ok(const char *v)
{
    static const char *const bad[] = { "javascript:", "mailto:", "tel:", "data:" };
    for (int b = 0; b < 4; b++) {
        int k = 0; while (bad[b][k] && lower(v[k]) == bad[b][k]) k++;
        if (!bad[b][k]) return 0;
    }
    return 1;
}

static void open_tag(const char *t, const char *a, u32 alen, int selfclose)
{
    int heading, tag = tag_id(t, &heading);
    char v[220];
    int is_void = !strcmp_(t, "br") || !strcmp_(t, "hr") || !strcmp_(t, "img") || !strcmp_(t, "input") || !strcmp_(t, "meta") ||
                  !strcmp_(t, "link") || !strcmp_(t, "base") || !strcmp_(t, "area") || !strcmp_(t, "col") || !strcmp_(t, "wbr") || !strcmp_(t, "source");
    if (tag == T_TITLE && !title[0]) { push_style(tag); in_title = 1; return; }   /* title лежит внутри скрытого head */
    if (hidden) return;
    cur_nh = nh_of(t);
    if (css_hide) { if (!is_void && !selfclose && tag != T_HIDE) push_style(tag); return; }
    css_t cs;
    if (tag != T_HIDE) get_css(t, a, alen, &cs); else cs.set = 0;
    if (cs.set & S_NODISP) {
        if (!is_void && !selfclose && tag != T_TITLE) { push_style(tag); st[sp_].nodisp = 1; css_hide++; }
        return;
    }
    if (!strcmp_(t, "br")) { emit_break(N_BR, 1); return; }
    if (!strcmp_(t, "hr")) { emit_break(N_HR, 0); return; }
    if (!strcmp_(t, "img")) {
        flush_word();
        char src[260], dim[12];
        int ok = nimg < MAXIMG && attr(a, alen, "src", src, sizeof src) && src[0] && !starts_(src, "data:");
        if (ok) {
            u16 aw = 0, ah = 0;
            if (attr(a, alen, "width", dim, sizeof dim)) aw = (u16)num_(dim);
            if (attr(a, alen, "height", dim, sizeof dim)) ah = (u16)num_(dim);
            if ((aw && aw <= 2) || (ah && ah <= 2)) return;                  /* счётчики-невидимки */
            char full[300], alt[90]; alt[0] = 0;
            net_url_join(base, src, full, sizeof full);
            attr(a, alen, "alt", alt, sizeof alt);
            char al2[44]; u32 al = 0;
            for (u32 k = 0; alt[k] && al < 40; k++) al = put_cp(al2, al, 44, (u8)alt[k] >= 0x80 ? '?' : (u8)alt[k]);
            al2[al] = 0;
            node_t *n = add_node(N_IMG);
            u32 uo = add_pool(full, slen(full)), ao = al ? add_pool(al2, al) : 0;
            if (n && uo) {
                img_t *m = &imgs[nimg];
                m->url = uo; m->alt = ao; m->aw = aw; m->ah = ah; m->nw = m->nh = 0; m->state = 0;
                n->aux = (i16)nimg++; n->link = st[sp_].link;
                return;
            }
        }
        node_t *n = add_node(N_WORD);
        if (n) {
            char alt[90], buf[110]; u32 l = 0;
            alt[0] = 0;
            attr(a, alen, "alt", alt, sizeof alt);
            const char *pre = "[image";
            while (*pre) buf[l++] = *pre++;
            if (alt[0]) {
                buf[l++] = ':'; buf[l++] = ' ';
                for (u32 k = 0; alt[k] && l < 100; k++) l = put_cp(buf, l, 104, (u8)alt[k] >= 0x80 ? '?' : (u8)alt[k]);
            }
            buf[l++] = ']'; buf[l] = 0;
            n->off = add_pool(buf, l); n->font = FONT_REGULAR; n->color = HC_GRAY; n->flags = F_SP; n->w = (u16)font_width(buf, FONT_REGULAR);
            n->link = st[sp_].link;
        }
        return;
    }
    if (is_void) return;
    if (tag == T_HIDE) { push_style(tag); hidden = 1; return; }
    if (tag == T_TITLE) { push_style(tag); in_title = 1; title[0] = 0; return; }
    if (selfclose) return;
    push_style(tag);
    style_t *s = &st[sp_];
    switch (tag) {
    case T_A:
        if (attr(a, alen, "href", v, sizeof v) && v[0] && v[0] != '#' && nlinks < MAXLINKS && link_ok(v)) {
            char full[300];
            net_url_join(base, v, full, sizeof full);
            u32 o = add_pool(full, slen(full));
            if (o) { s->link = (i16)nlinks; links[nlinks++] = &pool[o]; s->uline = 1; s->color = HC_LINK; }
        }
        break;
    case T_B: if (strcmp_(t, "i") && strcmp_(t, "em") && strcmp_(t, "cite") && strcmp_(t, "dfn")) s->font = FONT_BOLD; break;
    case T_CODE: s->font = FONT_MONO; break;
    case T_P: emit_break(N_PARA, 10); break;
    case T_DIV: emit_break(N_PARA, 0); break;
    case T_H:
        emit_break(N_PARA, heading <= 2 ? 16 : 12);
        s->font = heading == 1 ? FONT_TITLE : FONT_BOLD; s->color = HC_HEAD;
        break;
    case T_UL: case T_OL: {
        emit_break(N_PARA, 6);
        if (ol_depth < 7) { ol_depth++; ol_n[ol_depth] = 0; list_ord[ol_depth] = tag == T_OL; }
        node_t *n = add_node(N_INDENT); if (n) n->aux = 26;
        break;
    }
    case T_LI: {
        emit_break(N_PARA, 3);
        node_t *n = add_node(N_BULLET);
        if (n) {
            if (ol_depth > 0 && list_ord[ol_depth]) {
                char b[12]; int num = ++ol_n[ol_depth], l = 0, k = 0, d[8];
                while (num && k < 8) { d[k++] = num % 10; num /= 10; }
                while (k) b[l++] = (char)('0' + d[--k]);
                b[l++] = '.'; b[l] = 0;
                n->off = add_pool(b, (u32)l); n->w = (u16)font_width(b, FONT_REGULAR);
            } else n->aux = -1;
        }
        break;
    }
    case T_PRE: emit_break(N_PARA, 8); s->font = FONT_MONO; s->pre = 1; emit_break(N_PRE, 1); break;
    case T_BQ: {
        emit_break(N_PARA, 8);
        node_t *n = add_node(N_INDENT); if (n) n->aux = 26;
        n = add_node(N_QUOTE); if (n) n->aux = 1;
        break;
    }
    case T_TABLE: { emit_break(N_PARA, 8); emit_break(N_TBL, 0); node_t *n = &nodes[nn - 1]; if (nn > 0 && n->type == N_TBL) { if (cs.set & S_BG) n->off = cs.bg | 0x1000000u; if (cs.set & S_WIDTH) n->aux = cs.wd; } break; }
    case T_TR: { emit_break(N_TR, 0); node_t *n = &nodes[nn - 1]; if (nn > 0 && n->type == N_TR && (cs.set & S_BG)) n->off = cs.bg | 0x1000000u; break; }
    case T_TD: case T_TH: {
        int cs_n = 1;
        if (attr(a, alen, "colspan", v, sizeof v)) { cs_n = (int)num_(v); if (cs_n < 1) cs_n = 1; if (cs_n > 20) cs_n = 20; }
        emit_break(N_TD, cs_n);
        { node_t *tn = &nodes[nn - 1]; if (nn > 0 && tn->type == N_TD && (cs.set & S_BG)) tn->off = cs.bg | 0x1000000u; }
        if (tag == T_TH) s->font = FONT_BOLD;
        break;
    }
    case T_CENTER: emit_break(N_ALIGN, 1); s->center = 1; break;
    case T_DT: emit_break(N_PARA, 6); s->font = FONT_BOLD; break;
    case T_DD: { emit_break(N_PARA, 2); node_t *n = add_node(N_INDENT); if (n) n->aux = 26; break; }
    default: break;
    }
    if ((tag == T_DIV || tag == T_P || tag == T_TD || tag == T_TH) && attr(a, alen, "align", v, sizeof v) && lower(v[0]) == 'c') cs.set |= S_CENTER;
    /* CSS: цвета, жирность, выравнивание, фон */
    if ((cs.set & S_BG) && (tag == T_DIV || tag == T_P || tag == T_H || tag == T_BQ || tag == T_UL || tag == T_OL || tag == T_CENTER)) {
        if (!strcmp_(t, "body") || !strcmp_(t, "html")) { page_bg_rgb = cs.bg; if (lum(cs.bg) < 110) s->dark = 1; }
        else { s->dark = lum(cs.bg) < 110; if (!s->dark && s->has_rgb && lum(s->rgb) > 200) s->has_rgb = 0; s->bgo = 1; flush_word(); node_t *n = add_node(N_BGO); if (n) n->off = cs.bg | 0x1000000u; }
    }
    if (cs.set & S_COLOR) { if (!(lum(cs.color) > 200 && !s->dark)) { s->has_rgb = 1; s->rgb = cs.color; } }
    if ((cs.set & S_BOLD) && s->font != FONT_MONO) s->font = FONT_BOLD;
    if ((cs.set & S_NORMAL) && s->font == FONT_BOLD && tag != T_TH) s->font = FONT_REGULAR;
    if ((cs.set & S_BIG) && (s->font == FONT_REGULAR || s->font == FONT_BOLD)) s->font = FONT_TITLE;
    if (cs.set & S_NOUL) s->uline = 0;
    if (cs.set & S_UL) s->uline = 1;
    if ((cs.set & S_CENTER) && !(tag == T_CENTER)) { emit_break(N_ALIGN, 1); s->center = 1; s->ctr_own = 1; }
    if (tag == T_CENTER) s->ctr_own = 1;
}

static void close_tag(const char *t)
{
    int hd, tag = tag_id(t, &hd);
    int k = sp_;
    u16 nhc = nh_of(t);
    while (k > 0 && !(st[k].tag == tag && (tag != T_OTHER || st[k].nh == nhc))) k--;
    if (k == 0) return;
    while (sp_ >= k) {
        int tg = st[sp_].tag;
        if (tg == T_HIDE) hidden = 0;
        if (tg == T_TITLE) in_title = 0;
        if (st[sp_].nodisp && css_hide > 0) css_hide--;
        flush_word();
        if (!hidden && !css_hide) {
            switch (tg) {
            case T_P: emit_break(N_PARA, 10); break;
            case T_DIV: emit_break(N_PARA, 0); break;
            case T_H: emit_break(N_PARA, hd <= 2 ? 12 : 10); break;
            case T_UL: case T_OL: { if (ol_depth > 0) ol_depth--; node_t *n = add_node(N_INDENT); if (n) n->aux = -26; emit_break(N_PARA, 6); break; }
            case T_PRE: emit_break(N_PRE, 0); emit_break(N_PARA, 8); break;
            case T_BQ: { node_t *n = add_node(N_QUOTE); if (n) n->aux = 0; n = add_node(N_INDENT); if (n) n->aux = -26; emit_break(N_PARA, 8); break; }
            case T_TABLE: emit_break(N_TEND, 0); emit_break(N_PARA, 8); break;
            case T_DD: { node_t *n = add_node(N_INDENT); if (n) n->aux = -26; emit_break(N_PARA, 2); break; }
            case T_DT: emit_break(N_PARA, 2); break;
            default: break;
            }
            if (st[sp_].bgo) emit_break(N_BGC, 0);
            if (st[sp_].ctr_own) emit_break(N_ALIGN, sp_ > 0 ? st[sp_ - 1].center : 0);
        }
        flush_word();
        sp_--;
    }
}

void html_load(const char *src, u32 len, const char *base_url, int plain)
{
    nn = 0; pn = 1; nlinks = 0; nimg = 0; nb = 0; page_h = 0; trunc = 0; title[0] = 0; wl = 0; pend_space = 0;
    hidden = 0; in_title = 0; ol_depth = 0; sp_ = 0; css_hide = 0; nrules = 0; page_bg_rgb = 0xFFFFFF; cur_nh = 0;
    u32 bl = 0; while (base_url[bl] && bl < sizeof(base) - 1) { base[bl] = base_url[bl]; bl++; }
    base[bl] = 0;
    pool[0] = 0;
    { u8 *z = (u8 *)&st[0]; for (u32 q = 0; q < sizeof st[0]; q++) z[q] = 0; }
    st[0].font = FONT_REGULAR; st[0].color = HC_TEXT; st[0].link = -1;

    if (plain) {                                           /* text/plain: всё как есть, моноширинным шрифтом */
        push_style(T_PRE); st[sp_].font = FONT_MONO; st[sp_].pre = 1;
        emit_break(N_PRE, 1);
        emit_text(src, len);
        flush_word();
        emit_break(N_PRE, 0);
        return;
    }
    u32 i = 0, ts = 0;
    while (i < len) {
        if (src[i] != '<') { i++; continue; }
        if (i > ts) emit_text(src + ts, i - ts);
        u32 j = i + 1;
        if (j < len && src[j] == '!') {                    /* комментарий, doctype, CDATA */
            if (j + 2 < len && src[j+1] == '-' && src[j+2] == '-') {
                j += 3;
                while (j + 2 < len && !(src[j] == '-' && src[j+1] == '-' && src[j+2] == '>')) j++;
                j += 3;
            } else { while (j < len && src[j] != '>') j++; j++; }
            i = ts = j > len ? len : j; continue;
        }
        if (j < len && src[j] == '?') { while (j < len && src[j] != '>') j++; i = ts = (j < len ? j + 1 : len); continue; }
        int closing = 0;
        if (j < len && src[j] == '/') { closing = 1; j++; }
        if (j >= len || !((src[j] | 32) >= 'a' && (src[j] | 32) <= 'z')) {     /* просто '<' в тексте */
            emit_text("<", 1); i++; ts = i; continue;
        }
        char name[16]; u32 nl = 0;
        while (j < len && src[j] != ' ' && src[j] != '>' && src[j] != '/' && src[j] != '\n' && src[j] != '\t' && src[j] != '\r') {
            if (nl < sizeof(name) - 1) name[nl++] = (char)lower(src[j]);
            j++;
        }
        name[nl] = 0;
        u32 as = j;
        char q = 0;
        while (j < len && (src[j] != '>' || q)) {          /* конец тега с учётом кавычек */
            if (q) { if (src[j] == q) q = 0; }
            else if (src[j] == '"' || src[j] == '\'') q = src[j];
            j++;
        }
        u32 ae = j;
        int selfclose = ae > as && src[ae - 1] == '/';
        j = j < len ? j + 1 : len;
        if (closing) close_tag(name);
        else {
            if (nn >= MAXNODES - 4) { trunc = 1; break; }
            open_tag(name, src + as, ae - as, selfclose);
            if (!strcmp_(name, "script") || !strcmp_(name, "style") || !strcmp_(name, "textarea") || !strcmp_(name, "xmp")) {
                int hide = name[0] == 's';
                u32 k = j;                                  /* содержимое до закрывающего тега */
                while (k + 1 < len) {
                    if (src[k] == '<' && src[k+1] == '/') {
                        u32 z = 0; while (name[z] && lower(src[k + 2 + z]) == name[z]) z++;
                        if (!name[z]) break;
                    }
                    k++;
                }
                if (k + 1 >= len) k = len;
                if (!hide) emit_text(src + j, k - j);
                else if (!strcmp_(name, "style")) { char md[16]; if (!(attr(src + as, ae - as, "media", md, sizeof md) && starts_(md, "print"))) css_sheet(src + j, k - j); }
                j = k;
            }
        }
        i = ts = j;
    }
    if (ts < len && i >= len) emit_text(src + ts, len - ts);
    flush_word();
}

/* ---------- вёрстка ---------- */

static i32 spw[FONT_STYLES];
static i32 g_w, g_y, g_indent, g_x, g_gap, g_align, g_quote_y[4], g_nq;
static int line_first, line_n, pending_dot, pending_num_node;
static i32 pending_dot_x;
static int g_measure, g_tdepth;
static struct { int box; i32 y0, x, w; } bgs[8]; static int g_nbg;

static hbox_t *new_box(void)
{
    if (nb >= MAXBOXES) { trunc = 1; return 0; }
    hbox_t *b = &boxes[nb++];
    b->x = b->y = b->w = b->h = 0; b->text = 0; b->len = 0; b->kind = BK_TEXT; b->font = 0; b->color = 0; b->uline = 0; b->link = -1; b->rgb = 0;
    return b;
}

static i32 line_left(void) { return MARGIN + g_indent; }
static i32 line_avail(void) { i32 a = g_w - MARGIN - line_left(); return a < 40 ? 40 : a; }

/* Заканчивает строку: выравнивание по базовой линии и по центру. */
static void flush_line(void)
{
    if (line_n == 0 && !pending_dot) return;
    i32 asc = 0, hgt = 0, right = 0, left = 0x7FFFFFFF; int has_img = 0;
    for (int i = line_first; i < nb; i++) {
        hbox_t *b = &boxes[i];
        if (b->kind == BK_IMG) {
            if (b->h > asc) asc = b->h;
            if (b->h + 5 > hgt) hgt = b->h + 5;
            if (b->x + b->w > right) right = b->x + b->w;
            if (b->x < left) left = b->x;
            has_img = 1;
            continue;
        }
        if (b->kind != BK_TEXT) continue;
        i32 a = font_ascent[b->font], h = font_height[b->font];
        if (a > asc) asc = a;
        if (h > hgt) hgt = h;
        if (b->x + b->w > right) right = b->x + b->w;
        if (b->x < left) left = b->x;
    }
    if (hgt == 0) { hgt = font_height[FONT_REGULAR]; asc = font_ascent[FONT_REGULAR]; }
    if (has_img && hgt < asc + 5) hgt = asc + 5;
    for (int i = line_first; i + 1 < nb; i++) {              /* подчёркивание ссылки идёт сплошной линией через пробелы */
        hbox_t *a = &boxes[i], *b = &boxes[i + 1];
        if (a->kind == BK_TEXT && b->kind == BK_TEXT && a->link >= 0 && a->link == b->link && b->x > a->x + a->w) a->w = b->x - a->x;
    }
    if (g_gap && g_y > 0) g_y += g_gap;
    g_gap = 0;
    i32 shift = 0;
    if (g_align && right > 0 && !g_measure) { shift = line_left() + (line_avail() - (right - left)) / 2 - left; if (shift < 0) shift = 0; }
    for (int i = line_first; i < nb; i++) {
        hbox_t *b = &boxes[i];
        if (b->kind == BK_TEXT) { b->y = g_y + asc - font_ascent[b->font]; b->x += shift; }
        else if (b->kind == BK_IMG) { b->y = g_y + asc - b->h; b->x += shift; }
    }
    if (pending_dot) {                                    /* маркер списка у первой строки пункта */
        hbox_t *b = new_box();
        if (b) {
            if (pending_num_node >= 0) {
                const node_t *n = &nodes[pending_num_node];
                b->kind = BK_TEXT; b->text = &pool[n->off]; b->len = (u16)slen(b->text);
                b->font = FONT_REGULAR; b->w = n->w; b->h = font_height[FONT_REGULAR];
                b->x = pending_dot_x - n->w - 6; b->y = g_y + asc - font_ascent[FONT_REGULAR];
            } else { b->kind = BK_DOT; b->x = pending_dot_x - 15; b->y = g_y + asc - 8; b->w = 6; b->h = 6; }
        }
        pending_dot = 0;
    }
    g_y += hgt + 4;
    g_x = line_left();
    line_n = 0;
    line_first = nb;
}

static void put_text(const char *t, u32 len, i32 w, const node_t *n, i32 x)
{
    hbox_t *b = new_box();
    if (!b) return;
    b->kind = BK_TEXT; b->text = t; b->len = (u16)len; b->x = x; b->w = w; b->h = font_height[n->font];
    b->font = n->font; b->color = n->color; b->uline = (n->flags & F_ULINE) != 0; b->link = n->link; b->rgb = n->rgb;
    line_n++;
}

static void img_dims(int id, i32 *pw, i32 *ph)
{
    const img_t *m = &imgs[id];
    i32 avail = line_avail(), w, h, aw = m->aw, ah = m->ah;
    *pw = *ph = 0;
    if (m->state == 2) {
        if (!m->alt) return;
        w = font_width(&pool[m->alt], FONT_REGULAR) + 16; h = 22;
    } else if (m->state == 1) {
        if (m->nw <= 2 && m->nh <= 2) return;
        w = aw ? aw : (ah ? ah * m->nw / m->nh : m->nw);
        h = ah ? ah : (aw ? aw * m->nh / m->nw : m->nh);
    } else {
        w = aw ? aw : (ah ? ah * 4 / 3 : 64);
        h = ah ? ah : (aw ? aw * 3 / 4 : 40);
    }
    if (w > avail) { h = h * avail / w; w = avail; }
    if (h > 3000) h = 3000;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    *pw = w; *ph = h;
}

int html_image_count(void) { return nimg; }
const char *html_image_url(int i) { return i >= 0 && i < nimg ? &pool[imgs[i].url] : 0; }
int html_image_state(int i) { return i >= 0 && i < nimg ? imgs[i].state : 2; }
void html_image_set(int i, int nw, int nh)
{
    if (i < 0 || i >= nimg) return;
    if (nw <= 0 || nh <= 0) imgs[i].state = 2;
    else { imgs[i].state = 1; imgs[i].nw = (i16)(nw > 32000 ? 32000 : nw); imgs[i].nh = (i16)(nh > 32000 ? 32000 : nh); }
}

static void layout_table(int a, int b);
static int tbl_end(int i, int b);
static void layout_range(int a, int b)
{
    int in_pre = 0, bg_open = 0;
    i32 bg_y0 = 0;
    for (int ni = a; ni < b; ni++) {
        const node_t *n = &nodes[ni];
        switch (n->type) {
        case N_WORD: {
            const char *t = &pool[n->off];
            i32 w = n->w, sw = (n->flags & F_SP) && line_n > 0 ? spw[n->font] : 0;
            u32 tl = slen(t);
            if (in_pre) { put_text(t, tl, w, n, g_x); g_x += w; break; }
            if (g_x + sw + w > line_left() + line_avail() && line_n > 0) { flush_line(); sw = 0; }
            if (w > line_avail()) {                       /* слово длиннее строки: режем по символам */
                u32 pos = 0;
                while (pos < tl) {
                    i32 acc = 0; u32 end = pos;
                    while (end < tl) {
                        u32 cl = (u8)t[end] < 0x80 ? 1 : 2;
                        char tmp[3] = { t[end], cl == 2 ? t[end + 1] : 0, 0 };
                        i32 cw = font_width(tmp, n->font);
                        if (acc + cw > line_avail() && end > pos) break;
                        acc += cw; end += cl;
                    }
                    if (line_n > 0) flush_line();
                    put_text(t + pos, end - pos, acc, n, g_x);
                    g_x += acc;
                    pos = end;
                    if (pos < tl) flush_line();
                }
                break;
            }
            put_text(t, tl, w, n, g_x + sw);
            g_x += sw + w;
            break;
        }
        case N_IMG: {
            i32 w, h; img_dims(n->aux, &w, &h);
            if (w <= 0) break;
            i32 sw = line_n > 0 ? spw[FONT_REGULAR] : 0;
            if (g_x + sw + w > line_left() + line_avail() && line_n > 0) { flush_line(); sw = 0; }
            hbox_t *b = new_box();
            if (b) {
                const img_t *m = &imgs[n->aux];
                b->kind = BK_IMG; b->x = g_x + sw; b->w = w; b->h = h; b->color = (u8)n->aux; b->link = n->link;
                if (m->alt) { b->text = &pool[m->alt]; b->len = (u16)slen(b->text); }
                line_n++;
            }
            g_x += sw + w;
            break;
        }
        case N_GAP: if (line_n > 0) g_x += n->aux; break;
        case N_TBL: {
            int e = tbl_end(ni, b);
            layout_table(ni + 1, e);
            ni = e;
            break;
        }
        case N_BGO: {
            flush_line();
            if (g_nbg < 8) {
                i32 y0 = g_y + (g_gap && g_y > 0 ? g_gap : 0);
                hbox_t *b = new_box();
                bgs[g_nbg].box = -1;
                if (b) { b->kind = BK_NONE; b->rgb = n->off; bgs[g_nbg].box = (int)(b - boxes); line_first = nb; }
                bgs[g_nbg].y0 = y0; bgs[g_nbg].x = line_left() - 6; bgs[g_nbg].w = line_avail() + 12;
                g_nbg++;
            }
            break;
        }
        case N_BGC:
            flush_line();
            if (g_nbg > 0) {
                g_nbg--;
                if (bgs[g_nbg].box >= 0 && g_y > bgs[g_nbg].y0) {
                    hbox_t *b = &boxes[bgs[g_nbg].box];
                    b->kind = BK_BG; b->x = bgs[g_nbg].x; b->y = bgs[g_nbg].y0; b->w = bgs[g_nbg].w; b->h = g_y - bgs[g_nbg].y0;
                }
            }
            break;
        case N_TR: flush_line(); break;
        case N_TD: if (line_n > 0) g_x += 14; break;
        case N_TEND: break;
        case N_BR:
            if (line_n == 0 && !pending_dot) g_y += font_height[FONT_REGULAR] + 4;
            else flush_line();
            break;
        case N_PARA:
            flush_line();
            if (n->aux > g_gap) g_gap = n->aux;
            break;
        case N_HR: {
            flush_line();
            if (g_y > 0) g_y += 6;
            hbox_t *b = new_box();
            if (b) { b->kind = BK_HR; b->x = line_left(); b->y = g_y; b->w = line_avail(); b->h = 1; }
            g_y += 10; g_gap = 0;
            break;
        }
        case N_INDENT: flush_line(); g_indent += n->aux; if (g_indent < 0) g_indent = 0; g_x = line_left(); break;
        case N_BULLET:
            flush_line();
            pending_dot = 1; pending_dot_x = line_left(); pending_num_node = n->aux < 0 ? -1 : ni;
            break;
        case N_ALIGN: flush_line(); g_align = n->aux; break;
        case N_PRE:
            flush_line();
            if (n->aux) { in_pre = 1; bg_open = 1; bg_y0 = g_y + (g_gap && g_y > 0 ? g_gap : 0); }
            else {
                in_pre = 0;
                if (bg_open) {
                    hbox_t *b = new_box();
                    if (b) { b->kind = BK_BG; b->x = MARGIN - 8 + g_indent; b->y = bg_y0 - 6; b->w = g_w - 2 * MARGIN + 16 - g_indent; b->h = g_y - bg_y0 + 8; }
                    bg_open = 0;
                }
            }
            break;
        case N_QUOTE:
            flush_line();
            if (n->aux) { if (g_nq < 4) g_quote_y[g_nq++] = g_y + (g_gap && g_y > 0 ? g_gap : 0); }
            else if (g_nq > 0) {
                i32 y0 = g_quote_y[--g_nq];
                hbox_t *b = new_box();
                if (b) { b->kind = BK_BAR; b->x = MARGIN + g_indent - 18; b->y = y0; b->w = 3; b->h = g_y - y0; }
            }
            break;
        }
    }
}

static int tbl_end(int i, int b)
{
    int d = 1;
    for (int j = i + 1; j < b; j++) {
        if (nodes[j].type == N_TBL) d++;
        else if (nodes[j].type == N_TEND && --d == 0) return j;
    }
    return b;
}

/* Меряет ширину содержимого при заданной ширине области; ничего не оставляет в boxes. */
static i32 measure(int a, int b, i32 w)
{
    i32 sw_ = g_w, si = g_indent, sy = g_y, sx = g_x, sg = g_gap, sa = g_align, sln = line_n, slf = line_first, sm = g_measure, snq = g_nq;
    int spd = pending_dot, spn = pending_num_node, snb = nb, st_ = trunc, snbg = g_nbg;
    g_w = w + 2 * MARGIN; g_indent = 0; g_y = 0; g_gap = 0; g_align = 0; line_n = 0; line_first = nb; pending_dot = 0; g_measure = 1;
    g_x = line_left();
    layout_range(a, b);
    flush_line();
    i32 mx = 0;
    for (int i = snb; i < nb; i++) if (boxes[i].kind == BK_TEXT || boxes[i].kind == BK_IMG) { i32 r = boxes[i].x + boxes[i].w - MARGIN; if (r > mx) mx = r; }
    nb = snb; trunc = st_;
    g_w = sw_; g_indent = si; g_y = sy; g_x = sx; g_gap = sg; g_align = sa; line_n = sln; line_first = slf; g_measure = sm; g_nq = snq;
    pending_dot = spd; pending_num_node = spn; g_nbg = snbg;
    return mx;
}

#define TB_MAXC 160
#define TB_MAXCOLS 24
#define TB_DEPTH 4
typedef struct { int from, to; u8 row, col, span; u32 bg, rowbg; int bbox, rbox; } tcell_t;
static tcell_t tcells[TB_DEPTH][TB_MAXC];

static void layout_table(int a, int b)
{
    if (g_tdepth >= TB_DEPTH) { layout_range(a, b); return; }
    tcell_t *c = tcells[g_tdepth];
    int nc = 0, row = -1, col = 0, ncols = 0, cur = -1; u32 rowbg = 0;
    const node_t *tn = &nodes[a - 1];
    for (int i = a; i < b; i++) {
        int t = nodes[i].type;
        if (t == N_TBL) { i = tbl_end(i, b); if (cur >= 0) c[cur].to = i + 1; continue; }
        if (t == N_TR) { row++; col = 0; cur = -1; rowbg = nodes[i].off; continue; }
        if (t == N_TD && nc < TB_MAXC) {
            if (row < 0) { row = 0; col = 0; }
            int sp = nodes[i].aux > 0 ? nodes[i].aux : 1;
            if (col >= TB_MAXCOLS) continue;
            if (col + sp > TB_MAXCOLS) sp = TB_MAXCOLS - col;
            c[nc].from = i + 1; c[nc].to = i + 1; c[nc].row = (u8)row; c[nc].col = (u8)col; c[nc].span = (u8)sp; c[nc].bg = nodes[i].off; c[nc].rowbg = rowbg; c[nc].bbox = c[nc].rbox = -1;
            cur = nc++;
            col += sp; if (col > ncols) ncols = col;
            continue;
        }
        if (cur >= 0) c[cur].to = i + 1;
    }
    flush_line();
    if (g_gap && g_y > 0) g_y += g_gap;
    g_gap = 0;
    if (nc == 0) return;
    i32 W = line_avail(), left = line_left(), gap = 8, nat[TB_MAXCOLS], mn[TB_MAXCOLS], cw[TB_MAXCOLS], cx[TB_MAXCOLS];
    for (int i = 0; i < ncols; i++) nat[i] = mn[i] = 0;
    i32 cnat[TB_MAXC];
    for (int i = 0; i < nc; i++) {
        g_tdepth++; cnat[i] = measure(c[i].from, c[i].to, 3000); g_tdepth--;
        if (c[i].span == 1 && cnat[i] > nat[c[i].col]) nat[c[i].col] = cnat[i];
    }
    for (int i = 0; i < nc; i++) if (c[i].span > 1) {          /* ячейка на несколько колонок: добавляем недостающее последней */
        i32 have = gap * (c[i].span - 1);
        for (int k = 0; k < c[i].span; k++) have += nat[c[i].col + k];
        if (cnat[i] > have) nat[c[i].col + c[i].span - 1] += cnat[i] - have;
    }
    i32 sum = gap * (ncols - 1);
    for (int i = 0; i < ncols; i++) sum += nat[i];
    if (sum <= W) { for (int i = 0; i < ncols; i++) cw[i] = nat[i]; }
    else {
        for (int i = 0; i < nc; i++) if (c[i].span == 1) {
            g_tdepth++; i32 m = measure(c[i].from, c[i].to, 1); g_tdepth--;
            if (m > nat[c[i].col]) m = nat[c[i].col];
            if (m > mn[c[i].col]) mn[c[i].col] = m;
        }
        i32 av = W - gap * (ncols - 1), smin = 0, sd = 0;
        for (int i = 0; i < ncols; i++) { smin += mn[i]; sd += nat[i] - mn[i]; }
        for (int i = 0; i < ncols; i++) {
            if (smin >= av) cw[i] = smin > 0 ? mn[i] * av / smin : av / ncols;
            else cw[i] = mn[i] + (sd > 0 ? (nat[i] - mn[i]) * (av - smin) / sd : 0);
            if (cw[i] < 20 && nat[i] >= 20) cw[i] = 20;
        }
    }
    i32 total = gap * (ncols - 1);
    for (int i = 0; i < ncols; i++) total += cw[i];
    if (tn->aux) {
        i32 target = tn->aux < 0 ? W * -tn->aux / 100 : (tn->aux > W ? W : tn->aux);
        if (target > total) {
            i32 extra = target - total, base_sum = 0;
            for (int i = 0; i < ncols; i++) base_sum += cw[i];
            for (int i = 0; i < ncols; i++) cw[i] += base_sum > 0 ? extra * cw[i] / base_sum : extra / ncols;
            total = target;
        }
    }
    if (g_align && !g_measure && total < W) left += (W - total) / 2;
    i32 x = left, tbl_y0 = g_y; int tbox = -1;
    for (int i = 0; i < ncols; i++) { cx[i] = x; x += cw[i] + gap; }
    if (tn->off) { hbox_t *bb = new_box(); if (bb) { bb->kind = BK_NONE; bb->rgb = tn->off; tbox = (int)(bb - boxes); line_first = nb; } }
    i32 sw_ = g_w, si = g_indent;
    int r = 0, i = 0;
    while (i < nc) {
        r = c[i].row;
        i32 y0 = g_y, yend = y0; int i0 = i, rbox = -1;
        if (c[i].rowbg) { hbox_t *bb = new_box(); if (bb) { bb->kind = BK_NONE; bb->rgb = c[i].rowbg; rbox = (int)(bb - boxes); line_first = nb; } }
        for (; i < nc && c[i].row == r; i++) {
            if (c[i].bg) { hbox_t *bb = new_box(); if (bb) { bb->kind = BK_NONE; bb->rgb = c[i].bg; c[i].bbox = (int)(bb - boxes); line_first = nb; } }
            i32 w = 0;
            for (int k = 0; k < c[i].span; k++) w += cw[c[i].col + k] + (k ? gap : 0);
            g_indent = cx[c[i].col] - MARGIN; g_w = cx[c[i].col] + w + MARGIN;
            g_y = y0; g_gap = 0; g_align = 0; line_n = 0; line_first = nb; pending_dot = 0;
            g_x = line_left();
            int sbg = g_nbg;
            g_tdepth++; layout_range(c[i].from, c[i].to); flush_line(); g_tdepth--;
            g_nbg = sbg;
            if (g_y > yend) yend = g_y;
        }
        if (rbox >= 0) { hbox_t *bb = &boxes[rbox]; bb->kind = BK_BG; bb->x = left - 4; bb->y = y0; bb->w = total + 8; bb->h = yend + 2 - y0; }
        for (int q = i0; q < i; q++) if (c[q].bbox >= 0) {
            i32 w = 0; for (int k = 0; k < c[q].span; k++) w += cw[c[q].col + k] + (k ? gap : 0);
            hbox_t *bb = &boxes[c[q].bbox]; bb->kind = BK_BG; bb->x = cx[c[q].col] - 4; bb->y = y0; bb->w = w + 8; bb->h = yend + 2 - y0;
        }
        g_y = yend + 2;
    }
    if (tbox >= 0) { hbox_t *bb = &boxes[tbox]; bb->kind = BK_BG; bb->x = left - 4; bb->y = tbl_y0; bb->w = total + 8; bb->h = g_y - tbl_y0; }
    g_w = sw_; g_indent = si; g_gap = 0; g_align = 0;
    g_x = line_left(); line_n = 0; line_first = nb;
}

void html_layout(i32 width)
{
    g_w = width < 160 ? 160 : width;
    for (int f = 0; f < FONT_STYLES; f++) spw[f] = font_width(" ", f);
    g_nbg = 0; nb = 0; g_y = 14; g_indent = 0; g_gap = 0; g_align = 0; g_nq = 0; line_n = 0; line_first = 0; pending_dot = 0; pending_num_node = -1;
    g_x = line_left();
    layout_range(0, nn);
    flush_line();
    page_h = g_y + 24;
}


i32 html_height(void) { return page_h; }
int html_box_count(void) { return nb; }
const hbox_t *html_box(int i) { return &boxes[i]; }
const char *html_title(void) { return title; }
const char *html_link_url(int id) { return id >= 0 && id < nlinks ? links[id] : 0; }
int html_truncated(void) { return trunc; }
u32 html_page_bg(void) { return page_bg_rgb; }

int html_link_at(i32 x, i32 y)
{
    for (int i = nb - 1; i >= 0; i--) {
        const hbox_t *b = &boxes[i];
        if ((b->kind == BK_TEXT || b->kind == BK_IMG) && b->link >= 0 && x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h + 2) return b->link;
    }
    return -1;
}
