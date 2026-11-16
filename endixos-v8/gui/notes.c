/* Заметки: многострочный текст с переносом по словам. Сохраняется в ~/Notes.txt и на диск (/boot/notes.txt). */
#include "notes.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "resolution.h"
#include "../kernel/event.h"
#include "../kernel/fs.h"

#define MAXT 16000
#define LH 20
#define HEAD 36
#define PADX 14
#define MAXL 700

__attribute__((weak)) fat_t *res_mount(void) { return 0; }

typedef struct {
    char txt[MAXT + 1];
    int  len, cur, scroll, dirty, loaded;
    u32  edit_t, blink_t, saved_t;
    int  blink;
    i32  view_w, view_h;
    int  lstart[MAXL], nl;
    int  node, te;                           /* TextEdit (te = 1): файл в fs; node < 0 - файл пропал */
    u32  fsz;                                /* размер файла при последней синхронизации */
    char title[FS_NAME_MAX + 2];
} ed_t;

static ed_t NT = { .view_w = 520, .view_h = 400, .node = -1 }, TE = { .view_w = 560, .view_h = 400, .node = -1, .te = 1 };
static ed_t *E = &NT;
static u32 now_t;
static i32 adv[200];
#define txt (E->txt)
#define len (E->len)
#define cur (E->cur)
#define scroll (E->scroll)
#define dirty (E->dirty)
#define loaded (E->loaded)
#define edit_t (E->edit_t)
#define blink (E->blink)
#define blink_t (E->blink_t)
#define saved_t (E->saved_t)
#define view_w (E->view_w)
#define view_h (E->view_h)
#define lstart (E->lstart)
#define nl (E->nl)

static i32 cw(const char *p)                 /* ширина символа (ASCII или кириллица UTF-8) */
{
    int cp = utf8_cp(p), u;
    if (cp < 128) u = cp;
    else if (cp >= 0x410 && cp <= 0x44F) u = 128 + (cp - 0x410);
    else if (cp == 0x401) u = 192;
    else if (cp == 0x451) u = 193;
    else { cp = '?'; u = '?'; }
    if (!adv[u]) { char s[3]; int n = utf8_put(s, cp); s[n] = 0; adv[u] = font_width(s, FONT_REGULAR); if (adv[u] <= 0) adv[u] = 4; }
    return adv[u];
}

static void layout(void)                     /* начала строк с переносом по ширине */
{
    i32 maxw = view_w - 2 * PADX - 4;
    nl = 0; lstart[nl++] = 0;
    i32 x = 0; int lastsp = -1, i = 0;
    while (i < len && nl < MAXL - 1) {
        char c = txt[i]; int n = utf8_clen(&txt[i]);
        if (c == '\n') { lstart[nl++] = i + 1; x = 0; lastsp = -1; i++; continue; }
        i32 a = cw(&txt[i]);
        if (x + a > maxw && i > lstart[nl - 1]) {
            int br = (lastsp > lstart[nl - 1]) ? lastsp + 1 : i;
            lstart[nl++] = br; i = br; x = 0; lastsp = -1; continue;
        }
        if (c == ' ') lastsp = i;
        x += a; i += n;
    }
}

static int line_of(int pos)
{
    int l = 0;
    for (int k = 0; k < nl; k++) if (lstart[k] <= pos) l = k;
    return l;
}
static int lim(int l)                       /* позиция конца текста строки l (до перевода строки) */
{
    if (l + 1 >= nl) return len;
    int e = lstart[l + 1];
    return txt[e - 1] == '\n' ? e - 1 : e;
}

static i32 x_of(int l, int pos)
{
    i32 x = 0;
    for (int i = lstart[l]; i < pos && i < len; i += utf8_clen(&txt[i])) x += txt[i] == '\n' ? 0 : cw(&txt[i]);
    return x;
}

static int pos_at(int l, i32 px)              /* ближайшая позиция в строке l к x = px */
{
    int s = lstart[l], e = lim(l);
    i32 x = 0;
    for (int i = s; i < e; i += utf8_clen(&txt[i])) {
        if (txt[i] == '\n') return i;
        i32 a = cw(&txt[i]);
        if (px < x + a / 2) return i;
        x += a;
    }
    return e;
}

static void touch(void) { dirty = 1; edit_t = now_t; blink = 1; blink_t = now_t; layout(); }

static void ensure_visible(void)
{
    int rows = (view_h - HEAD - 16) / LH, l = line_of(cur);
    if (rows < 1) rows = 1;
    if (l < scroll) scroll = l;
    if (l >= scroll + rows) scroll = l - rows + 1;
    if (scroll < 0) scroll = 0;
}

static int note_node(void)
{
    int n = fs_lookup(fs_home(), "Notes.txt");
    return n;
}

static void save(void)
{
    if (E->te) {                                          /* TextEdit: свой файл */
        int n = E->node;
        if (n >= 0 && fs_valid(n) && !fs_is_dir(n)) {
            const char *nm = fs_name(n); int same = 1;
            for (int i = 0; i <= FS_NAME_MAX; i++) { if (nm[i] != E->title[i]) { same = 0; break; } if (!nm[i]) break; }
            if (!same) {                                  /* переименовали (размер тот же) или номер узла занял другой файл */
                if (fs_size(n) == E->fsz) { int k = 0; while (nm[k] && k < FS_NAME_MAX) { E->title[k] = nm[k]; k++; } E->title[k] = 0; }
                else E->node = n = -1;
            }
        } else E->node = n = -1;
        if (n >= 0) { fs_write(n, txt, (u32)len, 0); E->fsz = fs_size(n); }
        dirty = 0; saved_t = now_t;
        return;
    }
    int n = note_node();
    if (n < 0) n = fs_create(fs_home(), "Notes.txt");
    if (n >= 0) fs_write(n, txt, (u32)len, 0);
    fat_t *f = res_mount();
    if (f) fat_write(f, "/boot/notes.txt", (const u8 *)txt, (u32)len);
    dirty = 0; saved_t = now_t;
}

void notes_init(void)
{
    E = &NT;
    len = 0;
    fat_t *f = res_mount();
    int got = 0;
    if (f) {
        static u8 buf[MAXT + 1];
        int r = fat_read(f, "/boot/notes.txt", buf, MAXT);
        if (r > 0) { for (int i = 0; i < r; i++) txt[i] = (char)buf[i]; len = r; got = 1; }
    }
    if (!got) {
        int n = note_node();
        if (n >= 0 && !fs_is_dir(n)) {
            u32 sz = fs_size(n); if (sz > MAXT) sz = MAXT;
            const u8 *d = fs_data(n);
            for (u32 i = 0; i < sz; i++) txt[i] = (char)d[i];
            len = (int)sz;
        }
    }
    txt[len] = 0; cur = len; loaded = 1; layout(); ensure_visible();
}

static void insert(int cp)
{
    char b[3]; int n = utf8_put(b, cp);
    if (len + n > MAXT) return;
    for (int i = len + n - 1; i >= cur + n; i--) txt[i] = txt[i - n];
    for (int k = 0; k < n; k++) txt[cur + k] = b[k];
    cur += n; len += n; txt[len] = 0;
    touch();
}

static void erase(int pos)                  /* удаляет символ, начинающийся в pos */
{
    if (pos < 0 || pos >= len) return;
    int n = utf8_clen(&txt[pos]);
    for (int i = pos; i + n <= len; i++) txt[i] = txt[i + n];
    len -= n; if (cur > pos) cur = cur - n < pos ? pos : cur - n;
    txt[len] = 0;
    touch();
}

static int ed_key(i32 k, int ctrl)
{
    if (ctrl) {
        if (E->te && (k == 's' || k == 'S' || k == 19)) { save(); return 1; }
        return 0;
    }
    int l = line_of(cur);
    if (k == '\n' || k == '\r') insert('\n');
    else if (k == '\b') erase(utf8_prev(txt, cur));
    else if (k == KEY_DELETE) erase(cur);
    else if (k == KEY_LEFT) { if (cur > 0) cur = utf8_prev(txt, cur); blink = 1; blink_t = now_t; }
    else if (k == KEY_RIGHT) { if (cur < len) cur += utf8_clen(&txt[cur]); blink = 1; blink_t = now_t; }
    else if (k == KEY_UP) { if (l > 0) cur = pos_at(l - 1, x_of(l, cur)); else cur = 0; blink = 1; }
    else if (k == KEY_DOWN) { if (l + 1 < nl) cur = pos_at(l + 1, x_of(l, cur)); else cur = len; blink = 1; }
    else if (k == KEY_HOME) cur = lstart[l];
    else if (k == KEY_END) cur = lim(l);
    else if (k == '\t') { insert(' '); insert(' '); }
    else if (is_text_cp(k)) insert(k);
    else return 0;
    ensure_visible();
    return 1;
}

static int ed_click(i32 x, i32 y)
{
    if (y < HEAD) return 0;
    int l = scroll + (y - HEAD - 8) / LH;
    if (l < 0) l = 0;
    if (l >= nl) l = nl - 1;
    cur = pos_at(l, x - PADX);
    blink = 1; blink_t = now_t;
    return 1;
}

static int ed_tick(u32 ticks, int visible)
{
    now_t = ticks;
    if (!loaded) return 0;
    if (dirty && (i32)(ticks - edit_t) > 150) { save(); return visible; }
    if (visible && (i32)(ticks - blink_t) >= 50) { blink_t = ticks; blink ^= 1; return 1; }
    return 0;
}

static void ed_draw(i32 x, i32 y, i32 w, i32 h)
{
    const theme_t *t = theme();
    int dark = theme_is_dark();
    if (w != view_w || h != view_h) { view_w = w; view_h = h; layout(); ensure_visible(); }
    int te = E->te;
    u32 paper = te ? (dark ? RGB(30, 31, 34) : RGB(255, 255, 255)) : (dark ? RGB(44, 42, 34) : RGB(255, 249, 220));
    u32 band = te ? (dark ? RGB(48, 49, 54) : RGB(238, 238, 241)) : (dark ? RGB(70, 62, 30) : RGB(255, 226, 110));
    u32 tcol = te ? t->text : (dark ? RGB(255, 226, 140) : RGB(90, 70, 0));
    fb_fill_rect(x, y, w, h, paper);
    fb_fill_rect(x, y, w, HEAD, band);
    if (!te) font_draw(x + PADX, y + 9, "Notes", tcol, FONT_BOLD);
    char st[40]; int p = 0; const char *s = dirty ? "Editing..." : (te && E->node < 0 ? "File was removed" : saved_t ? "Saved" : (te ? "Ctrl+S to save" : ""));
    while (*s) st[p++] = *s++;
    st[p] = 0;
    font_draw(te ? x + PADX : x + w - PADX - font_width(st, FONT_REGULAR), y + 10, st, te ? t->sub : (dark ? RGB(200, 180, 110) : RGB(120, 100, 30)), FONT_REGULAR);
    int rows = (h - HEAD - 16) / LH;
    if (len == 0) font_draw(x + PADX, y + HEAD + 8, "Type something...", t->sub, FONT_REGULAR);
    for (int r = 0; r < rows; r++) {
        int l = scroll + r;
        if (l >= nl) break;
        i32 ly = y + HEAD + 8 + r * LH, lx = x + PADX;
        int e = lim(l);
        for (int i = lstart[l]; i < e; i += utf8_clen(&txt[i])) {
            if (txt[i] == '\n') break;
            char c[3] = { txt[i], 0, 0 };
            if (utf8_clen(&txt[i]) == 2) c[1] = txt[i + 1];
            else if ((unsigned char)c[0] < 32 || (unsigned char)c[0] > 126) c[0] = '?';
            if (c[0] != ' ') font_draw(lx, ly, c, t->text, FONT_REGULAR);
            lx += cw(&txt[i]);
        }
    }
    int cl = line_of(cur);
    if (blink && cl >= scroll && cl < scroll + rows)
        fb_fill_rect(x + PADX + x_of(cl, cur), y + HEAD + 8 + (cl - scroll) * LH + 1, 2, LH - 3, dark ? RGB(255, 214, 90) : RGB(220, 150, 0));
}

/* ---- публичные обёртки: выбирают экземпляр ---- */
int  notes_key(i32 k, int c)  { E = &NT; if (!loaded) notes_init(); return ed_key(k, c); }
int  notes_click(i32 x, i32 y){ E = &NT; if (!loaded) notes_init(); return ed_click(x, y); }
int  notes_tick(u32 t, int v) { E = &NT; return ed_tick(t, v); }
void notes_draw(i32 x, i32 y, i32 w, i32 h) { E = &NT; if (!loaded) notes_init(); ed_draw(x, y, w, h); }

int  te_key(i32 k, int c)   { E = &TE; return loaded ? ed_key(k, c) : 0; }
int  te_click(i32 x, i32 y) { E = &TE; return loaded ? ed_click(x, y) : 0; }
int  te_tick(u32 t, int v)  { E = &TE; return ed_tick(t, v); }
void te_draw(i32 x, i32 y, i32 w, i32 h) { E = &TE; if (loaded) ed_draw(x, y, w, h); }
const char *te_title(void)  { return TE.title; }

int te_is_text(int node)
{
    if (!fs_valid(node) || fs_is_dir(node)) return 0;
    const u8 *d = fs_data(node); u32 n = fs_size(node);
    if (n > MAXT) return 0;
    for (u32 i = 0; i < n; i++) if (d[i] == 0) return 0;
    return 1;
}

int te_open(int node)
{
    if (!te_is_text(node)) return -1;
    E = &TE;
    if (loaded && dirty) save();
    TE.node = node; TE.fsz = fs_size(node); len = 0;
    u32 sz = fs_size(node); const u8 *d = fs_data(node);
    for (u32 i = 0; i < sz; i++) txt[i] = (char)d[i];
    len = (int)sz; txt[len] = 0; cur = 0; scroll = 0; dirty = 0; saved_t = 0; loaded = 1;
    const char *nm = fs_name(node); int k = 0;
    while (nm[k] && k < FS_NAME_MAX) { TE.title[k] = nm[k]; k++; }
    TE.title[k] = 0;
    layout(); ensure_visible();
    return 0;
}
