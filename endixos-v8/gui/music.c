/* Music: плеер. Декодирует трек в 48 кГц стерео 16 бит и отдаёт драйверу HD Audio целиком. */
#include "music.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "ui.h"
#include "control.h"
#include "photos.h"
#include "../kernel/event.h"
#include "../kernel/fs.h"
#include "../kernel/blk.h"
#include "../drivers/hda.h"

__attribute__((weak)) int  hda_play_buf(const i16 *b, u32 n) { (void)b; (void)n; return 0; }
__attribute__((weak)) void hda_stop(void) {}
__attribute__((weak)) int  hda_present(void) { return 0; }
__attribute__((weak)) int  photos_volume(fat_t *f) { (void)f; return 0; }

#define RATE 48000
#define MAXF (RATE * 60)                         /* 60 секунд на трек */
#define RAWMAX (12 * 1024 * 1024)                /* WAV с диска читается целиком */
#define MAXT 24
#define ROWS 5
#define ROW_H 24

static i16 buf[(MAXF + 128) * 2] __attribute__((aligned(128)));
static u8  raw[RAWMAX];

typedef struct { char title[36]; char sub[24]; u8 kind; int node; char path[40]; } track_t;   /* kind: 0 встроенная, 1 ramfs, 2 диск */
static track_t tr[MAXT];
static int ntr, scanned, cur = -1, shown = -2, top;    /* cur - загруженный/играющий трек */
static u32 nframes;                              /* кадров в buf */
static int playing, bad;
static u32 now_t, t0, off;                       /* t0 - тик запуска, off - кадр, с которого запущено */
static char status[48];

/* ---- синтез встроенных мелодий ---- */
static i32 sin_q14(u32 ph)
{
    int neg = ph >= 32768;
    u32 x = ph & 0x7FFF;
    u32 u = (x * (32768 - x)) >> 15;
    i32 v = (i32)((16u * u << 14) / (5u * 32768 - 4u * u));
    v = v > 16384 ? 16384 : v;
    return neg ? -v : v;
}

typedef struct { u16 f; u8 q; } note_t;          /* частота, длительность в четвертях доли (4 = целая доля) */
#define N(f, q) { f, q }
static const note_t ode[] = {
    N(330,4),N(330,4),N(349,4),N(392,4),N(392,4),N(349,4),N(330,4),N(294,4),N(262,4),N(262,4),N(294,4),N(330,4),N(330,6),N(294,2),N(294,8),
    N(330,4),N(330,4),N(349,4),N(392,4),N(392,4),N(349,4),N(330,4),N(294,4),N(262,4),N(262,4),N(294,4),N(330,4),N(294,6),N(262,2),N(262,8) };
static const note_t twinkle[] = {
    N(262,4),N(262,4),N(392,4),N(392,4),N(440,4),N(440,4),N(392,8),N(349,4),N(349,4),N(330,4),N(330,4),N(294,4),N(294,4),N(262,8),
    N(392,4),N(392,4),N(349,4),N(349,4),N(330,4),N(330,4),N(294,8),N(392,4),N(392,4),N(349,4),N(349,4),N(330,4),N(330,4),N(294,8),
    N(262,4),N(262,4),N(392,4),N(392,4),N(440,4),N(440,4),N(392,8),N(349,4),N(349,4),N(330,4),N(330,4),N(294,4),N(294,4),N(262,8) };
static const note_t scale[] = {
    N(262,2),N(294,2),N(330,2),N(349,2),N(392,2),N(440,2),N(494,2),N(523,4),N(494,2),N(440,2),N(392,2),N(349,2),N(330,2),N(294,2),N(262,6),
    N(262,2),N(330,2),N(392,2),N(523,4),N(392,2),N(330,2),N(262,8) };
#define R(q) { 0, q }
static const note_t elise[] = {
    N(659,2),N(622,2),N(659,2),N(622,2),N(659,2),N(494,2),N(587,2),N(523,2),N(440,6),R(2),N(262,2),N(330,2),N(440,2),N(494,6),R(2),N(330,2),N(415,2),N(494,2),N(523,6),R(2),N(330,2),
    N(659,2),N(622,2),N(659,2),N(622,2),N(659,2),N(494,2),N(587,2),N(523,2),N(440,6),R(2),N(262,2),N(330,2),N(440,2),N(494,6),R(2),N(330,2),N(523,2),N(494,2),N(440,10),R(4),
    N(659,2),N(622,2),N(659,2),N(622,2),N(659,2),N(494,2),N(587,2),N(523,2),N(440,6),R(2),N(262,2),N(330,2),N(440,2),N(494,6),R(2),N(330,2),N(415,2),N(494,2),N(523,6),R(2),N(330,2),
    N(659,2),N(622,2),N(659,2),N(622,2),N(659,2),N(494,2),N(587,2),N(523,2),N(440,6),R(2),N(262,2),N(330,2),N(440,2),N(494,6),R(2),N(330,2),N(523,2),N(494,2),N(440,12) };
static const note_t jingle[] = {
    N(330,4),N(330,4),N(330,8),N(330,4),N(330,4),N(330,8),N(330,4),N(392,4),N(262,6),N(294,2),N(330,16),
    N(349,4),N(349,4),N(349,6),N(349,2),N(349,4),N(330,4),N(330,4),N(330,2),N(330,2),N(330,4),N(294,4),N(294,4),N(330,4),N(294,8),N(392,8),
    N(330,4),N(330,4),N(330,8),N(330,4),N(330,4),N(330,8),N(330,4),N(392,4),N(262,6),N(294,2),N(330,16),
    N(349,4),N(349,4),N(349,6),N(349,2),N(349,4),N(330,4),N(330,4),N(330,2),N(330,2),N(392,4),N(392,4),N(349,4),N(294,4),N(262,16) };
static const note_t greens[] = {
    N(440,4),N(523,8),N(587,4),N(659,6),N(698,2),N(659,4),N(587,8),N(494,4),N(392,6),N(440,2),N(494,4),N(523,8),N(440,4),N(440,6),N(415,2),N(440,4),N(494,8),N(415,4),N(330,8),R(4),
    N(440,4),N(523,8),N(587,4),N(659,6),N(698,2),N(659,4),N(587,8),N(494,4),N(392,6),N(440,2),N(494,4),N(523,6),N(494,2),N(440,4),N(415,6),N(370,2),N(415,4),N(440,12) };
static const note_t canon[] = {
    N(740,4),N(659,4),N(587,4),N(554,4),N(494,4),N(440,4),N(494,4),N(554,4),N(587,4),N(554,4),N(494,4),N(440,4),N(392,4),N(370,4),N(392,4),N(330,4),
    N(740,4),N(659,4),N(587,4),N(554,4),N(494,4),N(440,4),N(494,4),N(554,4),N(587,4),N(554,4),N(494,4),N(440,4),N(392,4),N(370,4),N(392,4),N(330,4),
    N(587,2),N(740,2),N(880,2),N(784,2),N(740,2),N(587,2),N(740,2),N(659,2),N(587,2),N(494,2),N(587,2),N(440,2),N(494,2),N(392,2),N(494,2),N(440,2),
    N(587,2),N(740,2),N(880,2),N(784,2),N(740,2),N(587,2),N(740,2),N(659,2),N(587,2),N(494,2),N(587,2),N(440,2),N(494,2),N(392,2),N(494,2),N(440,2),N(587,16) };
static const note_t king[] = {
    N(247,2),N(277,2),N(294,2),N(330,2),N(370,2),N(294,2),N(370,4),N(349,2),N(262,2),N(349,4),N(330,2),N(277,2),N(330,4),N(311,2),N(233,2),N(311,4),
    N(247,2),N(277,2),N(294,2),N(330,2),N(370,2),N(294,2),N(370,4),N(349,2),N(262,2),N(349,4),N(330,2),N(277,2),N(330,4),N(311,2),N(233,2),N(311,4),
    N(247,1),N(277,1),N(294,1),N(330,1),N(370,1),N(294,1),N(370,2),N(349,1),N(262,1),N(349,2),N(330,1),N(277,1),N(330,2),N(311,1),N(233,1),N(311,2),
    N(494,1),N(554,1),N(587,1),N(659,1),N(740,1),N(587,1),N(740,2),N(698,1),N(523,1),N(698,2),N(659,1),N(554,1),N(659,2),N(622,1),N(466,1),N(622,2),
    N(494,1),N(554,1),N(587,1),N(659,1),N(740,1),N(587,1),N(740,2),N(698,1),N(523,1),N(698,2),N(659,1),N(554,1),N(659,2),N(622,1),N(466,1),N(622,2),N(740,8) };
#define NTUNE 8
static const struct { const char *name; const note_t *n; int cnt; } tunes[NTUNE] = {
    { "Fur Elise", elise, (int)(sizeof elise / sizeof elise[0]) },
    { "Canon in D", canon, (int)(sizeof canon / sizeof canon[0]) },
    { "Greensleeves", greens, (int)(sizeof greens / sizeof greens[0]) },
    { "Jingle Bells", jingle, (int)(sizeof jingle / sizeof jingle[0]) },
    { "In the Hall of the Mountain King", king, (int)(sizeof king / sizeof king[0]) },
    { "Ode to Joy", ode, (int)(sizeof ode / sizeof ode[0]) },
    { "Twinkle, Twinkle", twinkle, (int)(sizeof twinkle / sizeof twinkle[0]) },
    { "Chime Scale", scale, (int)(sizeof scale / sizeof scale[0]) } };

static void synth(int k)
{
    const note_t *nt = tunes[k].n; int cnt = tunes[k].cnt;
    const u32 beat = 19200;                      /* 0,4 с */
    u32 pos = 0;
    for (u32 i = 0; i < (MAXF + 128) * 2; i++) buf[i] = 0;
    for (int n = 0; n < cnt; n++) {
        u32 len = beat * nt[n].q / 4;
        u32 ph = 0, step = ((u32)nt[n].f * 65536u) / RATE;
        i32 env = 7000;
        for (u32 i = 0; i < len && pos + i < MAXF; i++) {
            i32 s = (sin_q14(ph) * 3 + sin_q14(ph * 2)) >> 2;
            s = (s * env) >> 14;
            if (i < 400) s = s * (i32)i / 400;                  /* без щелчка на атаке */
            if (len - i < 1500) s = s * (i32)(len - i) / 1500;  /* и на спаде */
            buf[(pos + i) * 2] = buf[(pos + i) * 2 + 1] = (i16)s;
            ph = (ph + step) & 0xFFFF;
            env -= env >> 14;
        }
        pos += len;
    }
    nframes = pos > MAXF ? MAXF : pos;
}

/* ---- WAV ---- */
static u32 le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u32 le16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }

static i32 smp(const u8 *d, int bits, int ch, u32 i, int c)   /* кадр i, канал c -> 16 бит со знаком */
{
    u32 idx = i * (u32)ch + (u32)(c < ch ? c : 0);
    if (bits == 8) return ((i32)d[idx] - 128) << 8;
    return (i16)le16(d + idx * 2);
}

static int decode_wav(const u8 *d, u32 n)
{
    if (n < 44 || d[0] != 'R' || d[1] != 'I' || d[2] != 'F' || d[3] != 'F' || d[8] != 'W' || d[9] != 'A' || d[10] != 'V' || d[11] != 'E') return 0;
    u32 p = 12, rate = 0, dat = 0, dlen = 0; int ch = 0, bits = 0, fmt = 0;
    while (p + 8 <= n) {
        u32 cl = le32(d + p + 4);
        if (d[p] == 'f' && d[p + 1] == 'm' && d[p + 2] == 't' && d[p + 3] == ' ' && cl >= 16 && p + 8 + 16 <= n) {
            fmt = (int)le16(d + p + 8); ch = (int)le16(d + p + 10); rate = le32(d + p + 12); bits = (int)le16(d + p + 22);
        } else if (d[p] == 'd' && d[p + 1] == 'a' && d[p + 2] == 't' && d[p + 3] == 'a') {
            dat = p + 8; dlen = cl; if (dat + dlen > n) dlen = n - dat; break;
        }
        p += 8 + cl + (cl & 1);
    }
    if (fmt != 1 || (ch != 1 && ch != 2) || (bits != 8 && bits != 16) || rate < 4000 || rate > 192000 || !dat) return 0;
    u32 src = dlen / (u32)(ch * bits / 8);
    if (src < 2) return 0;
    const u8 *s = d + dat;
    u32 si = 0, acc = 0, o = 0;
    while (o < MAXF && si + 1 < src) {
        u32 f = (acc << 8) / RATE;                           /* линейная интерполяция между соседними кадрами */
        for (int c = 0; c < 2; c++) {
            i32 a = smp(s, bits, ch, si, c), b = smp(s, bits, ch, si + 1, c);
            buf[o * 2 + (u32)c] = (i16)(a + (((b - a) * (i32)f) >> 8));
        }
        o++;
        acc += rate;
        while (acc >= RATE) { acc -= RATE; si++; }
    }
    for (u32 i = o * 2; i < (o + 128) * 2; i++) buf[i] = 0;
    nframes = o;
    return 1;
}

/* ---- список треков ---- */
static int mlc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int is_wav(const char *s)
{
    int l = 0; while (s[l]) l++;
    return l > 4 && s[l - 4] == '.' && mlc(s[l - 3]) == 'w' && mlc(s[l - 2]) == 'a' && mlc(s[l - 1]) == 'v';
}
static void add_track(const char *name, const char *sub, int kind, int node, const char *path)
{
    if (ntr >= MAXT) return;
    track_t *t = &tr[ntr++];
    int l = 0; while (name[l]) l++;
    if (kind && l > 4) l -= 4;                                /* без .wav */
    if (l > 34) l = 34;
    for (int i = 0; i < l; i++) t->title[i] = name[i];
    t->title[l] = 0;
    int k = 0; while (sub[k] && k < 23) { t->sub[k] = sub[k]; k++; }
    t->sub[k] = 0;
    t->kind = (u8)kind; t->node = node;
    k = 0; if (path) while (path[k] && k < 39) { t->path[k] = path[k]; k++; }
    t->path[k] = 0;
}

static int on_entry(void *ctx, const char *name, const char *alias, u32 size, int is_dir)
{
    (void)ctx;
    if (!is_dir && size > 44 && size <= RAWMAX && is_wav(name)) {
        char p[40]; const char *pre = "/Music/"; int k = 0;
        for (; pre[k]; k++) p[k] = pre[k];
        for (int i = 0; alias[i] && k < 39; i++) p[k++] = alias[i];
        p[k] = 0;
        add_track(name, "Disk", 2, -1, p);
    }
    return ntr >= MAXT;
}

static void stop_now(void) { if (playing) hda_stop(); playing = 0; }

static void scan(void)
{
    stop_now();
    ntr = 0; cur = -1; top = 0; nframes = 0; bad = 0; status[0] = 0;
    for (int i = 0; i < NTUNE; i++) add_track(tunes[i].name, "Built-in", 0, i, 0);
    int d = fs_lookup(fs_home(), "Music");
    if (d >= 0 && fs_is_dir(d)) {
        int l[FS_MAX_NODES], n = fs_list(d, l, FS_MAX_NODES);
        for (int i = 0; i < n; i++) if (!fs_is_dir(l[i]) && is_wav(fs_name(l[i]))) add_track(fs_name(l[i]), "Music folder", 1, l[i], 0);
    }
    static fat_t f;
    if (photos_volume(&f)) fat_list(&f, "/Music", on_entry, 0);
    scanned = 1;
}

void music_init(void) { if (!scanned) scan(); }

/* ---- воспроизведение ---- */
static const char *msg(const char *s) { int i = 0; while (s[i] && i < 47) { status[i] = s[i]; i++; } status[i] = 0; return status; }

static int load(int k)
{
    stop_now();
    cur = k; bad = 0; nframes = 0; off = 0; status[0] = 0;
    track_t *t = &tr[k];
    int ok = 0;
    if (t->kind == 0) { synth(t->node); ok = 1; }
    else if (t->kind == 1) {
        if (fs_valid(t->node) && !fs_is_dir(t->node)) ok = decode_wav(fs_data(t->node), fs_size(t->node));
    } else {
        static fat_t f;
        if (photos_volume(&f)) { int n = fat_read(&f, t->path, raw, RAWMAX); if (n > 44) ok = decode_wav(raw, (u32)n); }
    }
    if (!ok) { bad = 1; msg("Can't play this file (WAV 8/16-bit only)"); return 0; }
    return 1;
}

static void start_at(u32 frame)
{
    if (cur < 0 || bad || !nframes) return;
    if (frame + 64 >= nframes) frame = 0;
    frame &= ~31u;                                           /* адрес буфера для DMA кратен 128 байт */
    off = frame; t0 = now_t; playing = 1;
    if (hda_present()) { if (!hda_play_buf(buf + frame * 2, nframes - frame)) msg("Audio device is busy"); else status[0] = 0; }
    else msg("No sound device found: playing silently");
}

static u32 pos_frames(void)
{
    if (!playing) return off;
    return off + (now_t - t0) * (RATE / 100);
}

static void play_track(int k) { if (k >= 0 && k < ntr && load(k)) start_at(0); }
static void toggle(void)
{
    if (cur < 0) { play_track(0); return; }
    if (bad) { play_track(cur); return; }
    if (playing) { u32 p = pos_frames(); stop_now(); off = p < nframes ? p : 0; }
    else start_at(off);
}
static void step(int d) { if (!ntr) return; int k = cur < 0 ? 0 : (cur + d + ntr) % ntr; play_track(k); }

int music_playing(void) { return playing; }
/* Для виджета: что сейчас выбрано и сколько проиграно (0..100). */
const char *music_now(int *pct, const char **sub)
{
    if (pct) { u32 p = pos_frames(); *pct = nframes ? (int)((u64)(p > nframes ? nframes : p) * 100 / nframes) : 0; }
    if (sub) *sub = cur >= 0 && cur < ntr ? tr[cur].sub : "";
    return cur >= 0 && cur < ntr ? tr[cur].title : "";
}

int music_tick(u32 ticks, int visible)
{
    now_t = ticks;
    if (!playing) return 0;
    if (pos_frames() >= nframes) {
        int next = cur + 1;
        stop_now();
        if (next < ntr) play_track(next); else { off = 0; }
        return 1;
    }
    return visible;
}

/* ---- интерфейс ---- */
#define ART 110
#define ARTY 14
#define BARY 192
#define BARX 28
#define CTLY 262
#define VOLY 302
#define LISTY 346

static void tri(i32 cx, i32 cy, i32 h, int dir, u32 col)       /* треугольник-«play»: dir +1 вправо, -1 влево */
{
    for (i32 i = 0; i < h; i++) {
        i32 half = i < h / 2 ? i : h - 1 - i;
        i32 len = half * 2 / 2 + 1;
        fb_fill_rect(dir > 0 ? cx - h / 3 + i * 0 + (h / 2 - half) * 0 : cx + h / 3 - len, cy - h / 2 + i, len, 1, col);
    }
}

static void tmfmt(char *b, u32 s)
{
    b[0] = (char)('0' + s / 60 / 10 % 10); b[1] = (char)('0' + s / 60 % 10); b[2] = ':'; b[3] = (char)('0' + s % 60 / 10); b[4] = (char)('0' + s % 10); b[5] = 0;
    if (b[0] == '0') { b[0] = b[1]; b[1] = b[2]; b[2] = b[3]; b[3] = b[4]; b[4] = 0; }
}

void music_draw(i32 x, i32 y, i32 w, i32 h)
{
    music_init();
    const theme_t *t = theme();
    int dark = theme_is_dark();
    u32 acc = theme_accent();
    fb_fill_rect(x, y, w, h, t->body);
    /* обложка: цветной квадрат с нотой */
    i32 ax = x + (w - ART) / 2, ay = y + ARTY;
    fb_fill_round_rect(ax + 3, ay + 8, ART - 6, ART - 6, 22, RGB(0, 0, 0), 40);
    fb_fill_round_rect(ax, ay, ART, ART, 22, acc, 255);
    fb_fill_round_rect(ax, ay, ART, ART / 2, 22, RGB(255, 255, 255), 36);
    fb_fill_circle(ax + 42, ay + 74, 15, RGB(255, 255, 255));
    fb_fill_rect(ax + 53, ay + 24, 5, 50, RGB(255, 255, 255));
    fb_fill_round_rect(ax + 53, ay + 24, 28, 12, 5, RGB(255, 255, 255), 255);
    /* название */
    const char *title = cur >= 0 ? tr[cur].title : "Music";
    const char *sub = cur >= 0 ? (bad ? "" : tr[cur].sub) : (ntr ? "Choose a track" : "");
    font_draw(x + (w - font_width(title, FONT_BOLD)) / 2, y + ARTY + ART + 12, title, t->text, FONT_BOLD);
    font_draw(x + (w - font_width(sub, FONT_REGULAR)) / 2, y + ARTY + ART + 34, sub, t->sub, FONT_REGULAR);
    /* полоса */
    u32 total = nframes, p = pos_frames();
    if (p > total) p = total;
    i32 bw = w - 2 * BARX;
    fb_fill_round_rect(x + BARX, y + BARY, bw, 6, 3, dark ? RGB(70, 70, 78) : RGB(208, 208, 216), 255);
    i32 fw = total ? (i32)((u32)bw * p / total) : 0;
    if (fw > 0) fb_fill_round_rect(x + BARX, y + BARY, fw < 6 ? 6 : fw, 6, 3, acc, 255);
    if (total) fb_fill_circle(x + BARX + fw, y + BARY + 3, 6, RGB(255, 255, 255));
    char a[8], b[8];
    tmfmt(a, p / RATE); tmfmt(b, total / RATE);
    font_draw(x + BARX, y + BARY + 14, a, t->sub, FONT_REGULAR);
    font_draw(x + w - BARX - font_width(b, FONT_REGULAR), y + BARY + 14, b, t->sub, FONT_REGULAR);
    /* кнопки */
    i32 cx = x + w / 2, cy = y + CTLY;
    u32 ink = t->text;
    fb_fill_circle(cx, cy, 26, acc);
    if (playing) { fb_fill_round_rect(cx - 9, cy - 10, 6, 20, 2, RGB(255, 255, 255), 255); fb_fill_round_rect(cx + 3, cy - 10, 6, 20, 2, RGB(255, 255, 255), 255); }
    else tri(cx + 2, cy, 22, 1, RGB(255, 255, 255));
    tri(cx - 74, cy, 16, -1, ink); fb_fill_rect(cx - 86, cy - 8, 3, 16, ink);
    tri(cx + 74, cy, 16, 1, ink); fb_fill_rect(cx + 83, cy - 8, 3, 16, ink);
    /* громкость */
    char v[16]; int vol = cc_volume(); int n = 0;
    const char *vl = "Volume "; while (*vl) v[n++] = *vl++;
    if (vol >= 100) v[n++] = '1'; if (vol >= 10) v[n++] = (char)('0' + vol / 10 % 10); v[n++] = (char)('0' + vol % 10); v[n++] = '%'; v[n] = 0;
    ui_button(x + w / 2 - 128, y + VOLY, 32, 24, "-", 1, 0);
    ui_button(x + w / 2 + 96, y + VOLY, 32, 24, "+", 1, 0);
    font_draw(x + (w - font_width(v, FONT_REGULAR)) / 2, y + VOLY + 4, v, t->sub, FONT_REGULAR);
    /* строка состояния и список */
    if (status[0]) font_draw(x + (w - font_width(status, FONT_REGULAR)) / 2, y + VOLY + 26, status, t->sub, FONT_REGULAR);
    ui_hline(x + 14, y + LISTY - 2, w - 28);
    if (cur != shown) { shown = cur; if (cur >= 0) { if (cur < top) top = cur; if (cur >= top + ROWS) top = cur - ROWS + 1; } }
    for (int r = 0; r < ROWS && top + r < ntr; r++) {
        int i = top + r; i32 ry = y + LISTY + r * ROW_H;
        if (i == cur) fb_fill_round_rect(x + 10, ry, w - 20, ROW_H - 2, 6, acc, 255);
        u32 col = i == cur ? RGB(255, 255, 255) : t->text;
        font_draw(x + 20, ry + 4, tr[i].title, col, FONT_REGULAR);
        font_draw(x + w - 20 - font_width(tr[i].sub, FONT_REGULAR), ry + 4, tr[i].sub, i == cur ? RGB(235, 235, 245) : t->sub, FONT_REGULAR);
    }
}

static int mhit(i32 px, i32 py, i32 x, i32 y, i32 w, i32 h) { return px >= x && px < x + w && py >= y && py < y + h; }

int music_click(i32 x, i32 y)
{
    music_init();
    i32 w = MUS_W, cx = w / 2;
    if (mhit(x, y, BARX - 6, BARY - 8, w - 2 * BARX + 12, 22) && cur >= 0 && !bad && nframes) {      /* перемотка */
        u32 f = (u32)(x - BARX < 0 ? 0 : x - BARX) * nframes / (u32)(w - 2 * BARX);
        if (f > nframes) f = nframes - 64;
        if (playing) { stop_now(); }
        start_at(f);
        return 1;
    }
    if (mhit(x, y, cx - 28, CTLY - 28, 56, 56)) { toggle(); return 1; }
    if (mhit(x, y, cx - 100, CTLY - 20, 50, 40)) { step(-1); return 1; }
    if (mhit(x, y, cx + 50, CTLY - 20, 50, 40)) { step(1); return 1; }
    if (mhit(x, y, cx - 128, VOLY, 32, 24)) { cc_set_volume(cc_volume() - 10); return 1; }
    if (mhit(x, y, cx + 96, VOLY, 32, 24)) { cc_set_volume(cc_volume() + 10); return 1; }
    if (y >= LISTY && y < LISTY + ROWS * ROW_H && x >= 10 && x < w - 10) {
        int i = top + (y - LISTY) / ROW_H;
        if (i < ntr) { if (i == cur && !bad) toggle(); else play_track(i); }
        return 1;
    }
    return 0;
}

int music_wheel(int d)                           /* колесо над списком: d > 0 - вверх */
{
    int m = ntr - ROWS; if (m < 0) m = 0;
    int t = top + (d > 0 ? -1 : 1);
    t = t < 0 ? 0 : t > m ? m : t;
    if (t == top) return 0;
    top = t; return 1;
}

int music_key(i32 k)
{
    music_init();
    if (k == ' ' || k == '\n') { toggle(); return 1; }
    if (k == KEY_RIGHT || k == 'n' || k == 'N') { step(1); return 1; }
    if (k == KEY_LEFT || k == 'p' || k == 'P') { step(-1); return 1; }
    if (k == KEY_UP) { cc_set_volume(cc_volume() + 5); return 1; }
    if (k == KEY_DOWN) { cc_set_volume(cc_volume() - 5); return 1; }
    if (k == 'r' || k == 'R') { scan(); return 1; }
    return 0;
}
