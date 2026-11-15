#include "usb.h"
#include "pci.h"
#include "pit.h"

/* Только опрос (без прерываний), память отображена 1:1. Перед каждой передачей асинхронное расписание
 * включается и после неё выключается: так очередь можно менять, не заботясь о кэше контроллера. */
#define MAXC 2
#define MAXD 4
#define NQ   8
#define CHUNK 16384

typedef struct { volatile u32 nxt, alt, tok, buf[5], ext[5], pad[3]; } qtd_t;
typedef struct { volatile u32 link, ch, caps, cur, nxt, alt, tok, buf[5], ext[5], pad[15]; } qh_t;
typedef struct {
    int used, ctrl, port, addr, msd, iface, iin, iout, tin, tout, mps;
    u16 vid, pid; u8 cls;
    char name[40];
    blk_t b;
} udev_t;

static qh_t  qh_head __attribute__((aligned(64))), qh_ep __attribute__((aligned(64)));
static qtd_t qtd[NQ] __attribute__((aligned(64)));
static u32   qlen[NQ];
static u8    sbuf[64] __attribute__((aligned(64))), mbuf[64] __attribute__((aligned(64)));
static u8    cbuf[512] __attribute__((aligned(512)));
static u8    dbuf[32768] __attribute__((aligned(4096)));

static volatile u8 *c_op[MAXC];
static int   c_np[MAXC], nc, cur_c;
static udev_t dv[MAXD];
static int   ndv, nmsd, next_addr, tag_ctr;
static char  lg[1400];
static int   lgn;

#define PA(p) ((u32)(unsigned long)(p))
#define BARRIER() __asm__ volatile("" ::: "memory")
#define CMD 0x00
#define STS 0x04
#define ASYNC 0x18
#define CFG 0x40
#define PSC(p) (0x44u + 4u * (u32)(p))
#define W1C 0x2Au                                  /* CSC | PEC | OCC: сбрасываются записью единицы */

#ifdef USB_TEST
void usb_idle(void);                               /* в хост-тесте здесь работает модель контроллера */
#else
static void usb_idle(void) { }
#endif

static void lc(char c) { if (lgn < (int)sizeof lg - 1) { lg[lgn++] = c; lg[lgn] = 0; } }
static void ls(const char *s) { while (*s) lc(*s++); }
static void ln(u32 v) { char t[12]; int l = 0; do { t[l++] = (char)('0' + v % 10); v /= 10; } while (v); while (l) lc(t[--l]); }
static void lx(u32 v, int d) { for (int i = d - 1; i >= 0; i--) lc("0123456789abcdef"[(v >> (i * 4)) & 15]); }

static inline u32 R(u32 o) { return *(volatile u32 *)(c_op[cur_c] + o); }
static inline void W(u32 o, u32 v) { *(volatile u32 *)(c_op[cur_c] + o) = v; }

static int expired(u32 t0, u32 *it, u32 ms) { return pit_ticks() - t0 > ms / 10 + 1 || ++*it > ms * 100000u; }
static void dly(u32 ms) { u32 t0 = pit_ticks(), it = 0; while (!expired(t0, &it, ms)) usb_idle(); }
static int wait_bit(u32 off, u32 mask, u32 val, u32 ms)
{
    u32 t0 = pit_ticks(), it = 0;
    while ((R(off) & mask) != val) { usb_idle(); if (expired(t0, &it, ms)) return -1; }
    return 0;
}

static void qtd_set(qtd_t *t, qtd_t *next, int pid, int dt, const void *buf, u32 len)
{
    u32 a = PA(buf);
    t->nxt = next ? PA(next) : 1; t->alt = 1;
    t->tok = 0x80u | (u32)pid << 8 | 3u << 10 | len << 16 | (u32)dt << 31;
    t->buf[0] = a;
    for (int i = 1; i < 5; i++) t->buf[i] = (a & ~0xFFFu) + 4096u * (u32)i;
    for (int i = 0; i < 5; i++) t->ext[i] = 0;
}

/* Выполняет цепочку qtd[0..n-1] на конечной точке ep устройства d. ctlmode: после короткого пакета управляется alt-ссылкой. */
static int run(udev_t *d, int ep, int mps, int n, int ctlmode, u32 ms, u32 *got)
{
    cur_c = d->ctrl;
    qh_t *q = &qh_ep;
    q->ch = (u32)d->addr | (u32)ep << 8 | 2u << 12 | 1u << 14 | (u32)mps << 16;
    q->caps = 1u << 30; q->cur = 0; q->nxt = PA(&qtd[0]); q->alt = 1; q->tok = 0;
    for (int i = 0; i < 5; i++) { q->buf[i] = 0; q->ext[i] = 0; }
    q->link = PA(&qh_head) | 2;
    qh_head.link = PA(q) | 2;
    BARRIER();
    W(ASYNC, PA(&qh_head));
    W(CMD, R(CMD) | (1u << 5));
    int res = 0;
    if (wait_bit(STS, 1u << 15, 1u << 15, 100)) res = -3;
    u32 t0 = pit_ticks(), it = 0;
    while (!res) {
        int stop = 0;
        for (int i = 0; i < n; i++) {
            u32 tk = qtd[i].tok;
            if (tk & 0x80) { stop = 0; break; }
            if (tk & 0x40) { res = (tk & 0x3C) ? -1 : -2; stop = 1; break; }        /* только Halted = STALL */
            stop = 1;
            if (!ctlmode && ((tk >> 16) & 0x7FFF) && i < n - 1) break;              /* короткий пакет: хвост не выполняется */
        }
        if (stop) break;
        usb_idle();
        if (expired(t0, &it, ms)) res = -3;
    }
    W(CMD, R(CMD) & ~(1u << 5));
    wait_bit(STS, 1u << 15, 0, 100);
    qh_head.link = PA(&qh_head) | 2;
    BARRIER();
    u32 g = 0;
    for (int i = 0; i < n; i++) { u32 tk = qtd[i].tok; if (!(tk & 0xC0)) g += qlen[i] - ((tk >> 16) & 0x7FFF); }
    if (got) *got = g;
    return res;
}

static int ctl(udev_t *d, u8 rt, u8 rq, u16 val, u16 idx, u16 len, void *data)
{
    int in = rt & 0x80;
    u8 *s = sbuf;
    s[0] = rt; s[1] = rq; s[2] = (u8)val; s[3] = (u8)(val >> 8); s[4] = (u8)idx; s[5] = (u8)(idx >> 8); s[6] = (u8)len; s[7] = (u8)(len >> 8);
    if (len > 512) return -1;
    if (!in && len) for (int i = 0; i < len; i++) cbuf[i] = ((u8 *)data)[i];
    int n = 0;
    qtd_set(&qtd[0], &qtd[1], 2, 0, s, 8); qlen[0] = 8; n = 1;
    if (len) {
        qtd_set(&qtd[1], &qtd[2], in ? 1 : 0, 1, cbuf, len); qlen[1] = len;
        qtd[1].alt = PA(&qtd[2]); n = 2;
    } else qtd[0].nxt = PA(&qtd[1]);
    qtd_set(&qtd[n], 0, in ? 0 : 1, 1, 0, 0); qlen[n] = 0; qtd[n].tok |= 1u << 15; n++;
    if (!len) qtd[0].nxt = PA(&qtd[1]);
    int r = run(d, 0, d->addr ? d->mps : 64, n, 1, 2000, 0);
    if (!r && in && len) for (int i = 0; i < len; i++) ((u8 *)data)[i] = cbuf[i];
    return r;
}

static int bulk(udev_t *d, int in, void *buf, u32 len, u32 *got)
{
    int ep = in ? d->iin : d->iout, *tg = in ? &d->tin : &d->tout;
    int t = *tg, n = 0;
    u32 off = 0;
    do {
        u32 c = len - off; if (c > CHUNK) c = CHUNK;
        qtd_set(&qtd[n], 0, in ? 1 : 0, t, (u8 *)buf + off, c); qlen[n] = c;
        if (n) qtd[n - 1].nxt = PA(&qtd[n]);
        u32 pk = c ? (c + (u32)d->mps - 1) / (u32)d->mps : 1;
        t ^= (int)(pk & 1);
        off += c; n++;
    } while (off < len && n < NQ);
    qtd[n - 1].tok |= 1u << 15;
    u32 g = 0;
    int r = run(d, ep, d->mps, n, 0, 5000, &g);
    if (!r) { u32 pk = g ? (g + (u32)d->mps - 1) / (u32)d->mps : 1; *tg ^= (int)(pk & 1); }
    if (got) *got = g;
    return r;
}

static void clear_halt(udev_t *d, int in)
{
    int ep = in ? d->iin : d->iout;
    ctl(d, 0x02, 1, 0, (u16)(ep | (in ? 0x80 : 0)), 0, 0);
    if (in) d->tin = 0; else d->tout = 0;
}

static void recover(udev_t *d)
{
    ctl(d, 0x21, 0xFF, 0, (u16)d->iface, 0, 0);
    clear_halt(d, 1); clear_halt(d, 0);
}

/* 0 успех, 1 команда завершилась с ошибкой, <0 сбой транспорта */
static int bot(udev_t *d, const u8 *cdb, int cl, void *data, u32 len, int in)
{
    u8 *c = mbuf;
    for (int i = 0; i < 31; i++) c[i] = 0;
    c[0] = 'U'; c[1] = 'S'; c[2] = 'B'; c[3] = 'C';
    u32 tag = ++tag_ctr;
    for (int i = 0; i < 4; i++) { c[4 + i] = (u8)(tag >> (8 * i)); c[8 + i] = (u8)(len >> (8 * i)); }
    c[12] = in ? 0x80 : 0; c[14] = (u8)cl;
    for (int i = 0; i < cl; i++) c[15 + i] = cdb[i];
    if (bulk(d, 0, c, 31, 0)) { recover(d); return -1; }
    if (len) {
        int r;
        if (!in) for (u32 i = 0; i < len; i++) dbuf[i] = ((u8 *)data)[i];
        r = bulk(d, in, dbuf, len, 0);
        if (r == -2) clear_halt(d, in);
        else if (r) { recover(d); return -1; }
        else if (in) for (u32 i = 0; i < len; i++) ((u8 *)data)[i] = dbuf[i];
    }
    int r = bulk(d, 1, mbuf, 13, 0);
    if (r == -2) { clear_halt(d, 1); r = bulk(d, 1, mbuf, 13, 0); }
    if (r) { recover(d); return -1; }
    if (mbuf[0] != 'U' || mbuf[1] != 'S' || mbuf[2] != 'B' || mbuf[3] != 'S' || mbuf[4] != (u8)tag) { recover(d); return -1; }
    return mbuf[12] == 0 ? 0 : mbuf[12] == 1 ? 1 : (recover(d), -1);
}

static int u_io(void *ctx, u32 lba, u32 cnt, void *buf, int wr)
{
    udev_t *d = ctx;
    if (!cnt || cnt > 64) return -1;
    u8 c[10] = { wr ? 0x2A : 0x28, 0, (u8)(lba >> 24), (u8)(lba >> 16), (u8)(lba >> 8), (u8)lba, 0, (u8)(cnt >> 8), (u8)cnt, 0 };
    int r = bot(d, c, 10, buf, cnt * 512, !wr);
    if (r) r = bot(d, c, 10, buf, cnt * 512, !wr);          /* одна повторная попытка */
    return r;
}
static int u_rd(void *ctx, u32 lba, u32 cnt, void *buf) { return u_io(ctx, lba, cnt, buf, 0); }
static int u_wr(void *ctx, u32 lba, u32 cnt, const void *buf) { return u_io(ctx, lba, cnt, (void *)buf, 1); }

static int msd_init(udev_t *d)
{
    u8 inq[36], c6[6] = { 0x12, 0, 0, 0, 36, 0 };
    if (bot(d, c6, 6, inq, 36, 1)) { ls("    INQUIRY failed\n"); return -1; }
    int l = 0;
    for (int part = 0; part < 2; part++) {                    /* производитель [8..16) и модель [16..32) без хвостовых пробелов */
        int a = part ? 16 : 8, e = part ? 32 : 16;
        while (e > a && (inq[e - 1] == ' ' || !inq[e - 1])) e--;
        if (part && l && e > a) d->name[l++] = ' ';
        for (int i = a; i < e && l < 38; i++) d->name[l++] = (char)(inq[i] >= 32 && inq[i] < 127 ? inq[i] : '?');
    }
    d->name[l] = 0;
    int ok = 0;
    for (int t = 0; t < 40 && !ok; t++) {
        u8 tur[6] = { 0, 0, 0, 0, 0, 0 }, rs[6] = { 3, 0, 0, 0, 18, 0 }, sn[18];
        int r = bot(d, tur, 6, 0, 0, 0);
        if (!r) { ok = 1; break; }
        if (r == 1) bot(d, rs, 6, sn, 18, 1);
        dly(100);
    }
    if (!ok) { ls("    not ready (no media?)\n"); return -1; }
    u8 cap[8], rc[10] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (bot(d, rc, 10, cap, 8, 1)) { ls("    READ CAPACITY failed\n"); return -1; }
    u32 last = (u32)cap[0] << 24 | (u32)cap[1] << 16 | (u32)cap[2] << 8 | cap[3];
    u32 bs = (u32)cap[4] << 24 | (u32)cap[5] << 16 | (u32)cap[6] << 8 | cap[7];
    if (bs != 512) { ls("    sector size "); ln(bs); ls(" not supported\n"); return -1; }
    d->b.rd = u_rd; d->b.wr = u_wr; d->b.ctx = d; d->b.sectors = last + 1;
    return 0;
}

static void enumerate(int c, int p)
{
    if (ndv >= MAXD) return;
    udev_t *d = &dv[ndv];
    for (unsigned i = 0; i < sizeof *d; i++) ((u8 *)d)[i] = 0;
    d->ctrl = c; d->port = p; d->mps = 64;
    cur_c = c;
    u8 dd[18];
    ls("  port "); ln((u32)p + 1); ls(": ");
    if (ctl(d, 0x80, 6, 0x0100, 0, 18, dd)) { ls("no answer to GET_DESCRIPTOR\n"); return; }
    d->mps = dd[7] ? dd[7] : 64;
    int a = next_addr++;
    if (ctl(d, 0x00, 5, (u16)a, 0, 0, 0)) { ls("SET_ADDRESS failed\n"); return; }
    d->addr = a; dly(20);
    if (ctl(d, 0x80, 6, 0x0100, 0, 18, dd)) { ls("cannot read device descriptor\n"); return; }
    d->vid = (u16)(dd[8] | dd[9] << 8); d->pid = (u16)(dd[10] | dd[11] << 8); d->cls = dd[4];
    u8 cf[512];
    if (ctl(d, 0x80, 6, 0x0200, 0, 9, cf)) { ls("cannot read configuration\n"); return; }
    u32 tl = (u32)(cf[2] | cf[3] << 8); if (tl > 512) tl = 512;
    if (ctl(d, 0x80, 6, 0x0200, 0, (u16)tl, cf)) { ls("cannot read configuration\n"); return; }
    int inif = 0;
    for (u32 o = 0; o + 2 <= tl && cf[o]; o += cf[o]) {
        if (cf[o + 1] == 4) {
            inif = cf[o + 5] == 8 && cf[o + 6] == 6 && cf[o + 7] == 0x50;
            if (inif) { d->msd = 1; d->iface = cf[o + 2]; }
        } else if (cf[o + 1] == 5 && inif && (cf[o + 3] & 3) == 2) {
            int mp = (cf[o + 4] | cf[o + 5] << 8) & 0x7FF;
            if (cf[o + 2] & 0x80) { d->iin = cf[o + 2] & 15; d->mps = mp; } else d->iout = cf[o + 2] & 15;
        }
    }
    ls("USB "); lx(d->vid, 4); lc(':'); lx(d->pid, 4);
    if (!d->msd || !d->iin || !d->iout) { ls(" class "); lx(d->cls, 2); ls(" - not a storage device, skipped\n"); return; }
    if (ctl(d, 0x00, 9, cf[5], 0, 0, 0)) { ls(" SET_CONFIGURATION failed\n"); return; }
    ls(" storage\n");
    if (msd_init(d)) return;
    d->used = 1; ndv++; nmsd++;
    ls("    "); ls(d->name); ls(", "); ln(d->b.sectors / 2048); ls(" MB\n");
}

static void port_scan(int c, int p)
{
    cur_c = c;
    u32 v = R(PSC(p));
    if (!(v & 1)) return;
    if (((v >> 10) & 3) == 1) { W(PSC(p), (v & ~W1C) | (1u << 13)); ls("  port "); ln((u32)p + 1); ls(": low-speed device (keyboard/mouse), left to the companion controller\n"); return; }
    W(PSC(p), (v & ~W1C & ~4u) | (1u << 8));                 /* сброс порта */
    dly(60);
    W(PSC(p), R(PSC(p)) & ~W1C & ~(1u << 8));
    wait_bit(PSC(p), 1u << 8, 0, 20);
    dly(20);
    v = R(PSC(p));
    W(PSC(p), (v & ~W1C) | (v & W1C));                         /* заодно сбросим флаги изменений */
    if (!(v & 4)) { W(PSC(p), (v & ~W1C) | (1u << 13)); ls("  port "); ln((u32)p + 1); ls(": full-speed device, left to the companion controller\n"); return; }
    enumerate(c, p);
}

static void ctrl_init(u32 bus, u32 dev, u32 fn)
{
    u32 bar = pci_read32(bus, dev, fn, 0x10) & ~0xFu;
    ls("EHCI at "); ln(bus); lc(':'); ln(dev); lc('.'); ln(fn);
    if (!bar) { ls(": BAR0 is empty\n"); return; }
    pci_enable(bus, dev, fn);
    volatile u8 *cap = (volatile u8 *)(unsigned long)bar;
    u32 caplen = cap[0], hcs = *(volatile u32 *)(cap + 4), hcc = *(volatile u32 *)(cap + 8);
    int c = nc;
    c_op[c] = cap + caplen; c_np[c] = (int)(hcs & 15); cur_c = c;
    ls(", "); ln((u32)c_np[c]); ls(" ports\n");
    u32 e = (hcc >> 8) & 0xFF;                                /* забираем контроллер у BIOS */
    for (int g = 0; e >= 0x40 && g < 16; g++) {
        u32 v = pci_read32(bus, dev, fn, e);
        if ((v & 0xFF) == 1) {
            if (v & (1u << 16)) {
                pci_write32(bus, dev, fn, e, v | (1u << 24));
                for (int i = 0; i < 100 && (pci_read32(bus, dev, fn, e) & (1u << 16)); i++) dly(10);
            }
            pci_write32(bus, dev, fn, e + 4, 0);
            break;
        }
        e = (v >> 8) & 0xFF;
    }
    W(CMD, R(CMD) & ~1u);
    wait_bit(STS, 1u << 12, 1u << 12, 100);
    W(CMD, R(CMD) | 2u);
    if (wait_bit(CMD, 2u, 0, 250)) { ls("  controller reset timed out\n"); return; }
    W(0x08, 0); W(0x10, 0);
    for (unsigned i = 0; i < sizeof qh_head; i++) ((volatile u8 *)&qh_head)[i] = 0;
    qh_head.link = PA(&qh_head) | 2; qh_head.ch = 1u << 15 | 2u << 12; qh_head.caps = 1u << 30;
    qh_head.nxt = 1; qh_head.alt = 1; qh_head.tok = 0x40;
    W(ASYNC, PA(&qh_head));
    W(CMD, 1u | 8u << 16);                                    /* пуск */
    if (wait_bit(STS, 1u << 12, 0, 100)) { ls("  controller does not start\n"); return; }
    W(CFG, 1);                                                /* все порты - этому контроллеру */
    dly(100);
    nc++;
    for (int p = 0; p < c_np[c]; p++) {
        if (hcs & 0x10) W(PSC(p), (R(PSC(p)) & ~W1C) | (1u << 12));
        dly(10);
    }
    dly(100);
    for (int p = 0; p < c_np[c]; p++) port_scan(c, p);
}

int usb_scan(void)
{
    ndv = 0; nmsd = 0; nc = 0; next_addr = 1; lgn = 0; lg[0] = 0;
    int found = 0;
    for (u32 bus = 0; bus < 4 && nc < MAXC; bus++)
        for (u32 dev = 0; dev < 32; dev++) {
            u32 nf = (pci_read32(bus, dev, 0, 0xC) & 0x800000) ? 8 : 1;
            for (u32 fn = 0; fn < nf && nc < MAXC; fn++) {
                u32 id = pci_read32(bus, dev, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                if ((pci_read32(bus, dev, fn, 8) >> 8) != 0x0C0320) continue;
                found++; ctrl_init(bus, dev, fn);
            }
        }
    if (!found) ls("no EHCI (USB 2.0) controller on the PCI bus\n");
    return nmsd;
}

int usb_drive_count(void) { return nmsd; }
static udev_t *drv(int i) { int k = 0; for (int j = 0; j < ndv; j++) if (dv[j].used && k++ == i) return &dv[j]; return 0; }
blk_t *usb_blk(int i) { udev_t *d = drv(i); return d ? &d->b : 0; }
const char *usb_drive_name(int i) { udev_t *d = drv(i); return d ? d->name : ""; }
const char *usb_log(void) { return lg; }
