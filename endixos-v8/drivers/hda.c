#include "hda.h"
#ifndef HDA_HOST
#include "pci.h"
#include "../kernel/io.h"
#endif

/* ---- доступ к регистрам ---- */
#define GCAP     0x00
#define GCTL     0x08
#define STATESTS 0x0E
#define INTCTL   0x20
#define CORBLBASE 0x40
#define CORBUBASE 0x44
#define CORBWP   0x48
#define CORBRP   0x4A
#define CORBCTL  0x4C
#define CORBSIZE 0x4E
#define RIRBLBASE 0x50
#define RIRBUBASE 0x54
#define RIRBWP   0x58
#define RINTCNT  0x5A
#define RIRBCTL  0x5C
#define RIRBSTS  0x5D
#define RIRBSIZE 0x5E
#define SD_CTL   0x00
#define SD_STS   0x03
#define SD_LPIB  0x04
#define SD_CBL   0x08
#define SD_LVI   0x0C
#define SD_FMT   0x12
#define SD_BDLPL 0x18
#define SD_BDLPU 0x1C

#ifdef HDA_HOST
extern u32 sim_rd(u32 off, int size);
extern void sim_wr(u32 off, int size, u32 v);
static u32  rd32(u32 o) { return sim_rd(o, 4); }
static u32  rd16(u32 o) { return sim_rd(o, 2); }
static u32  rd8(u32 o)  { return sim_rd(o, 1); }
static void wr32(u32 o, u32 v) { sim_wr(o, 4, v); }
static void wr16(u32 o, u32 v) { sim_wr(o, 2, v); }
static void wr8(u32 o, u32 v)  { sim_wr(o, 1, v); }
static void udelay(u32 n) { (void)n; }
#else
static volatile u8 *mm;
static u32  rd32(u32 o) { return *(volatile u32 *)(mm + o); }
static u32  rd16(u32 o) { return *(volatile u16 *)(mm + o); }
static u32  rd8(u32 o)  { return *(volatile u8 *)(mm + o); }
static void wr32(u32 o, u32 v) { *(volatile u32 *)(mm + o) = v; }
static void wr16(u32 o, u32 v) { *(volatile u16 *)(mm + o) = (u16)v; }
static void wr8(u32 o, u32 v)  { *(volatile u8 *)(mm + o) = (u8)v; }
static void udelay(u32 n) { while (n--) outb(0x80, 0); }          /* один outb на порт 0x80 ~ 1 мкс; PIT ещё не запущен */
#endif

/* ---- состояние ---- */
static int  present, have_path, codec = -1, corb_n, rirb_n, rirb_rp, npins_out, ndacs;
static u32  sdbase, oss, iss, vendor_id;
static int  playing, pending_start, vol_pct = 55, master_n;
static u32  t_start, t_len;                                        /* тиков */

static u32 corb[256] __attribute__((aligned(128)));
static u32 rirb[512] __attribute__((aligned(128)));                /* 256 записей по 8 байт */
static u32 bdl[8] __attribute__((aligned(128)));                   /* до двух записей по 16 байт */

#define PCM_FRAMES 48000                                           /* 1 с */
static i16 pcm[PCM_FRAMES * 2] __attribute__((aligned(128)));

/* ---- команды кодеку ---- */
static int verb(u32 nid, u32 v20, u32 *resp)
{
    u32 wp = ((u32)rd16(CORBWP) + 1) % (u32)corb_n;
    corb[wp] = ((u32)codec << 28) | (nid << 20) | (v20 & 0xFFFFF);
    wr16(CORBWP, wp);
    for (u32 i = 0; i < 20000; i++) {
        if ((rd16(RIRBWP) & 0xFF) != (u32)rirb_rp) {
            rirb_rp = (rirb_rp + 1) % rirb_n;
            if (resp) *resp = rirb[rirb_rp * 2];
            return 0;
        }
        udelay(1);
    }
    return -1;
}

static u32 param(u32 nid, u32 p) { u32 r = 0; verb(nid, 0xF0000 | p, &r); return r; }
static void set8(u32 nid, u32 verb12, u32 payload) { verb(nid, (verb12 << 8) | (payload & 0xFF), 0); }
static void set16(u32 nid, u32 verb4, u32 payload) { verb(nid, (verb4 << 16) | (payload & 0xFFFF), 0); }

/* ---- граф виджетов ---- */
#define MAXN 256
static u8  wtype[MAXN], wnconn[MAXN], wlong[MAXN];
static u8  wconn[MAXN][8];
static u32 wcaps[MAXN], wcfg[MAXN], wpincaps[MAXN];
static u32 first_w, nw, afg;
static u32 amp_out_caps, amp_in_caps;                              /* по умолчанию для функциональной группы */

static struct { u8 nid; u8 steps; u8 offset; u8 mute; } master[8];

static void amp_info(u32 nid, int out, u32 *offset, u32 *steps, u32 *mute)
{
    u32 caps = wcaps[nid];
    u32 c = (caps & 8) ? param(nid, out ? 0x12 : 0x0D) : (out ? amp_out_caps : amp_in_caps);   /* бит 3: свои параметры усилителя */
    *offset = c & 0x7F; *steps = (c >> 8) & 0x7F; *mute = c >> 31;
}

static void amp_out_set(u32 nid, u32 gain, int mute) { set16(nid, 0x3, 0xB000 | (mute ? 0x80 : 0) | (gain & 0x7F)); }
static void amp_in_set(u32 nid, u32 idx, u32 gain, int mute) { set16(nid, 0x3, 0x7000 | (idx << 8) | (mute ? 0x80 : 0) | (gain & 0x7F)); }

static void read_conns(u32 nid)
{
    u32 len = param(nid, 0x0E);
    wlong[nid] = (u8)((len >> 7) & 1);
    u32 n = len & 0x7F;
    if (n > 8) n = 8;
    wnconn[nid] = (u8)n;
    u32 per = wlong[nid] ? 2 : 4;
    for (u32 i = 0; i < n; i += per) {
        u32 r = 0;
        verb(nid, 0xF0200 | i, &r);
        for (u32 k = 0; k < per && i + k < n; k++)
            wconn[nid][i + k] = wlong[nid] ? (u8)(r >> (16 * k)) : (u8)(r >> (8 * k));
    }
}

/* Находит путь от вывода pin к ЦАП: заполняет path (от ЦАП к выходу). Глубина не больше 5. */
static int find_dac(u32 nid, int depth, u8 *path, int *plen)
{
    if (depth > 5 || nid >= MAXN) return 0;
    if (wtype[nid] == 0) { path[0] = (u8)nid; *plen = 1; return 1; }
    for (u32 i = 0; i < wnconn[nid]; i++) {
        u32 c = wconn[nid][i];
        if (c < first_w || c >= first_w + nw) continue;
        if (find_dac(c, depth + 1, path, plen)) {
            path[(*plen)++] = (u8)nid;
            set8(nid, 0x701, i);                                   /* выбрать этот вход, если у виджета их несколько */
            return 1;
        }
    }
    return 0;
}

static void unmute_path(const u8 *path, int len)
{
    for (int k = 0; k < len; k++) {
        u32 nid = path[k], off, steps, mute;
        set8(nid, 0x705, 0);                                        /* питание D0 */
        if (wcaps[nid] & 4) { amp_info(nid, 1, &off, &steps, &mute); amp_out_set(nid, off, 0); }   /* 0 дБ, без mute */
        if (wcaps[nid] & 2) {                                       /* усилители входов миксеров и селекторов */
            amp_info(nid, 0, &off, &steps, &mute);
            for (u32 i = 0; i < wnconn[nid] || i < 1; i++) amp_in_set(nid, i, off, 0);
        }
    }
}

static void add_master(u32 nid)
{
    u32 off, steps, mute;
    if (master_n >= 8 || !(wcaps[nid] & 4)) return;
    amp_info(nid, 1, &off, &steps, &mute);
    if (steps == 0) return;
    master[master_n].nid = (u8)nid; master[master_n].steps = (u8)steps; master[master_n].offset = (u8)off; master[master_n].mute = (u8)mute;
    master_n++;
}

void hda_set_volume(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    vol_pct = pct;
    if (!have_path) return;
    for (int i = 0; i < master_n; i++) {
        u32 g = (u32)master[i].steps * (u32)pct / 100;
        amp_out_set(master[i].nid, g, pct == 0 && master[i].mute);
    }
}

static int configure_codec(void)
{
    u32 r = param(0, 0x04);
    u32 fg_start = (r >> 16) & 0xFF, fg_n = r & 0xFF;
    afg = 0;
    for (u32 i = 0; i < fg_n; i++)
        if ((param(fg_start + i, 0x05) & 0xFF) == 1) { afg = fg_start + i; break; }
    if (!afg) return 0;
    verb(0, 0xF0000, &vendor_id);
    set8(afg, 0x705, 0);
    udelay(1000);
    r = param(afg, 0x04);
    first_w = (r >> 16) & 0xFF; nw = r & 0xFF;
    if (first_w + nw > MAXN) nw = MAXN - first_w;
    amp_out_caps = param(afg, 0x12);
    amp_in_caps = param(afg, 0x0D);
    for (u32 n = first_w; n < first_w + nw; n++) {
        wcaps[n] = param(n, 0x09);
        wtype[n] = (u8)((wcaps[n] >> 20) & 0xF);
        if (wcaps[n] & 0x100) read_conns(n);                        /* бит 8: есть список подключений */
        if (wtype[n] == 4) { wpincaps[n] = param(n, 0x0C); verb(n, 0xF1C00, &wcfg[n]); }
        if (wtype[n] == 0) ndacs++;
    }
    /* выходные выводы: не "нет подключения", способны выводить, устройство - линейный выход, динамик или наушники */
    for (u32 n = first_w; n < first_w + nw; n++) {
        if (wtype[n] != 4 || !(wpincaps[n] & 0x10)) continue;
        u32 conn = wcfg[n] >> 30, dev = (wcfg[n] >> 20) & 0xF;
        if (conn == 1 || (dev != 0 && dev != 1 && dev != 2)) continue;
        u8 path[8]; int plen = 0;
        if (!find_dac(n, 0, path, &plen)) continue;
        path[plen++] = (u8)n;
        unmute_path(path, plen);
        set8(n, 0x707, dev == 2 ? 0xC0 : 0x40);                     /* OUT_EN (+ HP_EN у наушников) */
        if (wpincaps[n] & 0x10000) set8(n, 0x70C, 2);               /* EAPD: усилитель динамика */
        set8(path[0], 0x706, 0x10);                                 /* ЦАП: поток 1, канал 0 */
        set16(path[0], 0x2, 0x0011);                                /* 48 кГц, 16 бит, стерео */
        add_master(path[0]);
        npins_out++;
    }
    if (npins_out && master_n == 0)                                 /* регулировка только на выводах */
        for (u32 n = first_w; n < first_w + nw; n++)
            if (wtype[n] == 4 && (wpincaps[n] & 0x10)) add_master(n);
    return npins_out;
}

/* ---- поток ---- */
static void stream_reset(void)
{
    wr32(sdbase + SD_CTL, rd32(sdbase + SD_CTL) & ~2u);              /* остановить */
    wr32(sdbase + SD_CTL, (rd32(sdbase + SD_CTL) & 0xFFFFFFu) | 1u);
    for (u32 i = 0; i < 2000 && !(rd32(sdbase + SD_CTL) & 1); i++) udelay(1);
    wr32(sdbase + SD_CTL, rd32(sdbase + SD_CTL) & ~1u);
    for (u32 i = 0; i < 2000 && (rd32(sdbase + SD_CTL) & 1); i++) udelay(1);
}

static void stream_stop(void)
{
    if (!present) return;
    wr32(sdbase + SD_CTL, rd32(sdbase + SD_CTL) & ~2u);
    playing = 0;
}

static void stream_start(u32 frames)
{
    stream_stop();
    stream_reset();
    u32 bytes = (frames * 4 + 127) & ~127u;
    bdl[0] = (u32)(unsigned long)pcm; bdl[1] = 0; bdl[2] = bytes; bdl[3] = 0;
    wr32(sdbase + SD_BDLPL, (u32)(unsigned long)bdl);
    wr32(sdbase + SD_BDLPU, 0);
    wr32(sdbase + SD_CBL, bytes);
    wr16(sdbase + SD_LVI, 0);
    wr16(sdbase + SD_FMT, 0x0011);
    wr32(sdbase + SD_CTL, (1u << 20) | 2u);                          /* тег потока 1, RUN */
    playing = 1;
    pending_start = 1;
    t_len = frames;                                                   /* кадров: длительность считаем при первом hda_poll */
}

/* ---- синтез звуков (целые числа, без float) ---- */
static i32 sin_q14(u32 ph)                                           /* ph: 0..65535 - оборот; результат -16384..16384 */
{
    int neg = ph >= 32768;
    u32 x = ph & 0x7FFF;
    u32 u = (x * (32768 - x)) >> 15;                                  /* t(1-t) в Q15, максимум 8192 */
    i32 v = (i32)((16u * u << 14) / (5u * 32768 - 4u * u) );          /* 16u/(5-4u): синус полуволны */
    v = v > 16384 ? 16384 : v;
    return neg ? -v : v;
}

static u32 synth(const u32 *freq, const u32 *delay, int nf, u32 frames, u32 amp, u32 decay_shift)
{
    for (u32 i = 0; i < frames * 2; i++) pcm[i] = 0;
    for (int k = 0; k < nf; k++) {
        u32 ph = 0, step = (freq[k] * 65536u) / 48000u;
        i32 env = (i32)amp;
        for (u32 i = delay[k]; i < frames; i++) {
            i32 s = (sin_q14(ph) * env) >> 14;
            i32 l = pcm[i * 2] + s;
            if (l > 32767) l = 32767;
            if (l < -32768) l = -32768;
            pcm[i * 2] = pcm[i * 2 + 1] = (i16)l;
            ph = (ph + step) & 0xFFFF;
            env -= env >> decay_shift;
        }
    }
    return frames;
}

void hda_play_chime(void)
{
    if (!have_path || playing) return;
    static const u32 f[4] = { 523, 659, 784, 1046 }, d[4] = { 0, 2400, 4800, 7200 };    /* до-ми-соль-до по очереди */
    u32 n = synth(f, d, 4, 36000, 5200, 13);
    stream_start(n);
}

void hda_play_pop(void)
{
    if (!have_path || playing) return;
    static const u32 f[1] = { 880 }, d[1] = { 0 };
    u32 n = synth(f, d, 1, 3800, 9000, 9);
    stream_start(n);
}

void hda_poll(u32 now_ticks, u32 hz)
{
    if (!playing) return;
    if (pending_start) {
        t_start = now_ticks;
        t_len = (t_len * hz + 47999) / 48000 + hz / 20;               /* длительность + 50 мс запаса */
        pending_start = 0;
    }
    if ((i32)(now_ticks - t_start) >= (i32)t_len) stream_stop();
}

int hda_present(void) { return present && have_path; }
int hda_playing(void) { return playing; }

/* ---- инициализация ---- */
static int ctrl_reset(void)
{
    wr32(GCTL, rd32(GCTL) & ~1u);
    for (u32 i = 0; i < 1000 && (rd32(GCTL) & 1); i++) udelay(10);
    wr32(GCTL, rd32(GCTL) | 1u);
    for (u32 i = 0; i < 1000 && !(rd32(GCTL) & 1); i++) udelay(10);
    if (!(rd32(GCTL) & 1)) return 0;
    udelay(2000);                                                     /* кодеки сообщают о себе в течение ~0.5 мс */
    for (u32 i = 0; i < 100 && !(rd16(STATESTS) & 0x7FFF); i++) udelay(100);
    u32 st = rd16(STATESTS) & 0x7FFF;
    if (!st) return 0;
    for (codec = 0; codec < 15 && !(st & (1u << codec)); codec++) ;
    return 1;
}

static int rings_init(void)
{
    u32 cs = rd8(CORBSIZE), rs = rd8(RIRBSIZE);
    wr8(CORBCTL, 0); wr8(RIRBCTL, 0);
    for (u32 i = 0; i < 1000 && ((rd8(CORBCTL) | rd8(RIRBCTL)) & 2); i++) udelay(10);
    if (cs & 0x40) { wr8(CORBSIZE, 2); corb_n = 256; } else if (cs & 0x20) { wr8(CORBSIZE, 1); corb_n = 16; } else return 0;
    if (rs & 0x40) { wr8(RIRBSIZE, 2); rirb_n = 256; } else if (rs & 0x20) { wr8(RIRBSIZE, 1); rirb_n = 16; } else return 0;
    wr32(CORBLBASE, (u32)(unsigned long)corb); wr32(CORBUBASE, 0);
    wr32(RIRBLBASE, (u32)(unsigned long)rirb); wr32(RIRBUBASE, 0);
    wr16(CORBRP, 0x8000);
    for (u32 i = 0; i < 1000 && !(rd16(CORBRP) & 0x8000); i++) udelay(10);
    wr16(CORBRP, 0);
    for (u32 i = 0; i < 1000 && (rd16(CORBRP) & 0x8000); i++) udelay(10);
    wr16(CORBWP, 0);
    wr16(RIRBWP, 0x8000);
    rirb_rp = 0;
    wr16(RINTCNT, 1);
    wr8(CORBCTL, 2);
    wr8(RIRBCTL, 2);
    return 1;
}

static int setup(void)
{
    u32 gcap = rd16(GCAP);
    oss = (gcap >> 12) & 0xF; iss = (gcap >> 8) & 0xF;
    if (!oss) return 0;
    sdbase = 0x80 + 0x20 * iss;                                       /* первый выходной поток */
    wr32(INTCTL, 0);
    if (!ctrl_reset() || !rings_init()) return 0;
    present = 1;
    if (!configure_codec()) return 0;
    have_path = 1;
    hda_set_volume(vol_pct);
    return 1;
}

#ifdef HDA_HOST
int hda_attach(void) { return setup(); }
#else
static int find_hda(u32 *rb, u32 *rd_, u32 *rf)
{
    for (u32 b = 0; b < 8; b++)
        for (u32 d = 0; d < 32; d++)
            for (u32 f = 0; f < 8; f++) {
                u32 id = pci_read32(b, d, f, 0);
                if ((id & 0xFFFF) == 0xFFFF) { if (f == 0) break; continue; }
                if ((pci_read32(b, d, f, 8) >> 8) == 0x040300) { *rb = b; *rd_ = d; *rf = f; return 1; }
                if (f == 0 && !(pci_read32(b, d, 0, 0x0C) >> 16 & 0x80)) break;
            }
    return 0;
}

int hda_init(void)
{
    u32 b, d, f;
    if (!find_hda(&b, &d, &f)) return 0;
    pci_enable(b, d, f);
    u32 bar0 = pci_read32(b, d, f, 0x10);
    if (bar0 & 1) return 0;
    mm = (volatile u8 *)(bar0 & ~0xFu);
    return setup();
}
#endif

void hda_diag(char *out, int cap)
{
    static const char *const yes = "yes", *const no = "no";
    char *p = out;
    const char *parts[8];
    int n = 0;
    parts[n++] = "HD Audio controller: ";
    parts[n++] = present ? yes : no;
    parts[n++] = ", output path: ";
    parts[n++] = have_path ? yes : no;
    parts[n++] = "\n";
    for (int i = 0; i < n; i++)
        for (const char *q = parts[i]; *q && p - out < cap - 1; q++) *p++ = *q;
    char num[24];
    int l = 0;
    u32 v = (u32)npins_out;
    do { num[l++] = (char)('0' + v % 10); v /= 10; } while (v);
    const char *lab = "outputs: ";
    for (const char *q = lab; *q && p - out < cap - 1; q++) *p++ = *q;
    while (l && p - out < cap - 1) *p++ = num[--l];
    if (p - out < cap - 1) *p++ = '\n';
    *p = 0;
}
