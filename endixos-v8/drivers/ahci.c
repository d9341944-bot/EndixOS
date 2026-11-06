#include "ahci.h"
#include "pci.h"

#define MAXD 4
static ahci_disk_t disks[MAXD]; static int nd;
static volatile u8 *abar;
static const char *stat_msg = "not initialised";

/* Буферы: список команд (1 КБ), приём FIS (256 Б), таблица команд и данные на каждый порт. */
static u8 clb[MAXD][1024] __attribute__((aligned(1024)));
static u8 fis[MAXD][256] __attribute__((aligned(256)));
static u8 ctb[MAXD][256] __attribute__((aligned(128)));
static u8 dbuf[MAXD][64 * 512] __attribute__((aligned(2)));
static int dport[MAXD];

static inline u32 rd(u32 off) { return *(volatile u32 *)(abar + off); }
static inline void wr(u32 off, u32 v) { *(volatile u32 *)(abar + off) = v; }
#define PR(p, r) (0x100u + 0x80u * (u32)(p) + (r))

static int wait_clear(u32 off, u32 mask, u32 loops)
{
    while (loops--) { if (!(rd(off) & mask)) return 0; for (volatile int i = 0; i < 200; i++) { } }
    return -1;
}

static void port_stop(int p)
{
    u32 c = rd(PR(p, 0x18));
    wr(PR(p, 0x18), c & ~1u);                              /* ST = 0 */
    wait_clear(PR(p, 0x18), 1u << 15, 5000);               /* CR */
    c = rd(PR(p, 0x18));
    wr(PR(p, 0x18), c & ~(1u << 4));                       /* FRE = 0 */
    wait_clear(PR(p, 0x18), 1u << 14, 5000);               /* FR */
}

static int port_start(int slot, int p)
{
    port_stop(p);
    for (u32 i = 0; i < sizeof clb[slot]; i++) clb[slot][i] = 0;
    for (u32 i = 0; i < sizeof fis[slot]; i++) fis[slot][i] = 0;
    wr(PR(p, 0x00), (u32)clb[slot]); wr(PR(p, 0x04), 0);
    wr(PR(p, 0x08), (u32)fis[slot]); wr(PR(p, 0x0C), 0);
    wr(PR(p, 0x30), 0xFFFFFFFFu);                          /* SERR */
    wr(PR(p, 0x10), 0xFFFFFFFFu);                          /* IS */
    wr(PR(p, 0x14), 0);                                    /* без прерываний */
    wr(PR(p, 0x18), rd(PR(p, 0x18)) | (1u << 4));          /* FRE */
    wr(PR(p, 0x18), rd(PR(p, 0x18)) | 1u);                 /* ST */
    return 0;
}

/* Одна команда; write - направление данных; возвращает 0 при успехе. */
static int issue(int slot, u8 cmd, u32 lba, u32 count, u32 bytes, int write)
{
    int p = dport[slot];
    u8 *h = clb[slot], *t = ctb[slot];
    for (int i = 0; i < 256; i++) t[i] = 0;
    for (int i = 0; i < 32; i++) h[i] = 0;
    h[0] = 5 | (write ? 0x40 : 0);                         /* CFL = 5 двойных слов, бит W */
    h[2] = 1; h[3] = 0;                                    /* PRDTL = 1 */
    *(u32 *)(h + 8) = (u32)t; *(u32 *)(h + 12) = 0;
    t[0] = 0x27; t[1] = 0x80; t[2] = cmd;
    t[4] = (u8)lba; t[5] = (u8)(lba >> 8); t[6] = (u8)(lba >> 16); t[7] = 0x40;
    t[8] = (u8)(lba >> 24); t[9] = 0; t[10] = 0;
    t[12] = (u8)count; t[13] = (u8)(count >> 8);
    if (cmd == 0xEC) { t[7] = 0; t[12] = 0; t[13] = 0; }
    *(u32 *)(t + 0x80) = (u32)dbuf[slot]; *(u32 *)(t + 0x84) = 0; *(u32 *)(t + 0x88) = 0;
    *(u32 *)(t + 0x8C) = (bytes - 1) & 0x3FFFFF;
    if (wait_clear(PR(p, 0x20), 0x88, 20000)) return -1;   /* BSY | DRQ */
    wr(PR(p, 0x10), 0xFFFFFFFFu);
    wr(PR(p, 0x38), 1);                                    /* CI */
    for (u32 i = 0; i < 2000000; i++) {
        if (!(rd(PR(p, 0x38)) & 1)) {
            return (rd(PR(p, 0x20)) & 0x01) || (rd(PR(p, 0x10)) & (1u << 30)) ? -2 : 0;
        }
        if (rd(PR(p, 0x10)) & (1u << 30)) return -2;       /* TFES */
    }
    return -3;
}

static int find_ahci(u32 *rb, u32 *rd_, u32 *rf)
{
    for (u32 bus = 0; bus < 4; bus++)
        for (u32 dev = 0; dev < 32; dev++) {
            u32 nf = 1;
            if (pci_read32(bus, dev, 0, 0xC) & 0x800000) nf = 8;
            for (u32 fn = 0; fn < nf; fn++) {
                u32 id = pci_read32(bus, dev, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                if ((pci_read32(bus, dev, fn, 8) >> 8) == 0x010601) { *rb = bus; *rd_ = dev; *rf = fn; return 1; }
            }
        }
    return 0;
}

int ahci_init(void)
{
    nd = 0;
    u32 bus = 0, dev = 0, fn = 0;
    if (!find_ahci(&bus, &dev, &fn)) { stat_msg = "no AHCI controller (class 0106) on the PCI bus"; return 0; }
    u32 bar = pci_read32(bus, dev, fn, 0x24) & ~0xFu;
    if (!bar) { stat_msg = "AHCI: BAR5 is empty"; return 0; }
    pci_enable(bus, dev, fn);
    abar = (volatile u8 *)bar;
    wr(0x04, rd(0x04) | (1u << 31));                       /* GHC.AE */
    u32 pi = rd(0x0C);
    for (int p = 0; p < 32 && nd < MAXD; p++) {
        if (!(pi & (1u << p))) continue;
        u32 ssts = rd(PR(p, 0x28));
        if ((ssts & 0xF) != 3 || ((ssts >> 8) & 0xF) != 1) continue;     /* нет связи */
        if (rd(PR(p, 0x24)) != 0x00000101) continue;                      /* не обычный SATA-диск */
        int slot = nd;
        dport[slot] = p;
        port_start(slot, p);
        if (issue(slot, 0xEC, 0, 0, 512, 0)) continue;
        u16 *id = (u16 *)dbuf[slot];
        ahci_disk_t *d = &disks[nd];
        d->port = p;
        for (int i = 0; i < 20; i++) { d->model[i * 2] = (char)(id[27 + i] >> 8); d->model[i * 2 + 1] = (char)(id[27 + i] & 0xFF); }
        d->model[40] = 0;
        for (int i = 39; i > 0 && d->model[i] == ' '; i--) d->model[i] = 0;
        u32 s28 = (u32)id[60] | (u32)id[61] << 16, s48 = (u32)id[100] | (u32)id[101] << 16;
        d->sectors = (id[83] & (1u << 10)) && s48 ? s48 : s28;
        nd++;
    }
    stat_msg = nd ? "ok" : "AHCI controller found, but no SATA disk is connected";
    return nd;
}

int ahci_disk_count(void) { return nd; }
const ahci_disk_t *ahci_disk(int i) { return i >= 0 && i < nd ? &disks[i] : 0; }
const char *ahci_status(void) { return stat_msg; }

int ahci_read(int d, u32 lba, u32 count, void *buf)
{
    if (d < 0 || d >= nd || count == 0 || count > 64) return -1;
    int r = issue(d, 0x25, lba, count, count * 512, 0);
    if (r) return r;
    u8 *s = dbuf[d], *o = buf;
    for (u32 i = 0; i < count * 512; i++) o[i] = s[i];
    return 0;
}

int ahci_write(int d, u32 lba, u32 count, const void *buf)
{
    if (d < 0 || d >= nd || count == 0 || count > 64) return -1;
    const u8 *s = buf; u8 *o = dbuf[d];
    for (u32 i = 0; i < count * 512; i++) o[i] = s[i];
    return issue(d, 0x35, lba, count, count * 512, 1);
}
