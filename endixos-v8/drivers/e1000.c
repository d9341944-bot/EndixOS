#include "e1000.h"
#include "pci.h"

#define R_CTRL 0x0000
#define R_STATUS 0x0008
#define R_IMC 0x00D8
#define R_RCTL 0x0100
#define R_TCTL 0x0400
#define R_TIPG 0x0410
#define R_RDBAL 0x2800
#define R_RDBAH 0x2804
#define R_RDLEN 0x2808
#define R_RDH 0x2810
#define R_RDT 0x2818
#define R_TDBAL 0x3800
#define R_TDBAH 0x3804
#define R_TDLEN 0x3808
#define R_TDH 0x3810
#define R_TDT 0x3818
#define R_MTA 0x5200
#define R_RAL 0x5400
#define R_RAH 0x5404

#define NRX 32
#define NTX 8
#define BUFSZ 2048

typedef struct { u32 lo, hi; u16 len; u16 csum; u8 status, errors; u16 special; } __attribute__((packed)) rxd_t;
typedef struct { u32 lo, hi; u16 len; u8 cso, cmd, status, css; u16 special; } __attribute__((packed)) txd_t;

static volatile u8 *mmio;
static u8 mymac[6];
static int ok;
static rxd_t rxd[NRX] __attribute__((aligned(16)));
static txd_t txd[NTX] __attribute__((aligned(16)));
static u8 rxbuf[NRX][BUFSZ] __attribute__((aligned(16)));
static u8 txbuf[NTX][BUFSZ] __attribute__((aligned(16)));
static u32 rx_cur, tx_cur;

static u32 rd(u32 r) { return *(volatile u32 *)(mmio + r); }
static void wr(u32 r, u32 v) { *(volatile u32 *)(mmio + r) = v; }

static const u16 IDS[] = { 0x100E, 0x100F, 0x1004, 0x10D3, 0x1503 };

int e1000_init(void)
{
    u32 b = 0, d = 0, f = 0;
    int found = 0;
    for (u32 i = 0; i < sizeof(IDS) / sizeof(IDS[0]) && !found; i++)
        found = pci_find(0x8086, IDS[i], &b, &d, &f);
    if (!found) return 0;
    pci_enable(b, d, f);
    u32 bar0 = pci_read32(b, d, f, 0x10);
    if (bar0 & 1) return 0;                            /* порт ввода-вывода, а не память */
    mmio = (volatile u8 *)(bar0 & ~0xFu);

    wr(R_IMC, 0xFFFFFFFFu);                            /* без прерываний */
    wr(R_CTRL, rd(R_CTRL) | 1u << 26);                 /* RST */
    for (volatile u32 i = 0; i < 200000 && (rd(R_CTRL) & 1u << 26); i++) ;
    wr(R_IMC, 0xFFFFFFFFu);
    wr(R_CTRL, (rd(R_CTRL) | 1u << 6 | 1u << 5) & ~(1u << 31 | 1u << 30 | 1u << 3));   /* SLU, ASDE; без reset/PHY reset */

    u32 lo = rd(R_RAL), hi = rd(R_RAH);
    mymac[0] = (u8)lo; mymac[1] = (u8)(lo >> 8); mymac[2] = (u8)(lo >> 16); mymac[3] = (u8)(lo >> 24);
    mymac[4] = (u8)hi; mymac[5] = (u8)(hi >> 8);

    for (u32 i = 0; i < 128; i++) wr(R_MTA + i * 4, 0);

    for (u32 i = 0; i < NRX; i++) { rxd[i].lo = (u32)(unsigned long)rxbuf[i]; rxd[i].hi = 0; rxd[i].status = 0; }
    wr(R_RDBAL, (u32)(unsigned long)rxd); wr(R_RDBAH, 0);
    wr(R_RDLEN, NRX * 16);
    wr(R_RDH, 0); wr(R_RDT, NRX - 1);
    rx_cur = 0;
    /* EN, широковещание (BAM), буфер 2048 (BSIZE=0), без CRC в данных (SECRC) */
    wr(R_RCTL, 1u << 1 | 1u << 15 | 1u << 26);

    for (u32 i = 0; i < NTX; i++) { txd[i].lo = (u32)(unsigned long)txbuf[i]; txd[i].hi = 0; txd[i].status = 1; txd[i].cmd = 0; }
    wr(R_TDBAL, (u32)(unsigned long)txd); wr(R_TDBAH, 0);
    wr(R_TDLEN, NTX * 16);
    wr(R_TDH, 0); wr(R_TDT, 0);
    tx_cur = 0;
    wr(R_TIPG, 10 | 8 << 10 | 6 << 20);
    wr(R_TCTL, 1u << 1 | 1u << 3 | 0x0F << 4 | 0x40 << 12);   /* EN, PSP, CT=15, COLD=64 */

    ok = 1;
    return 1;
}

void e1000_mac(u8 mac[6]) { for (int i = 0; i < 6; i++) mac[i] = mymac[i]; }

int e1000_link(void) { return ok && (rd(R_STATUS) & 2u); }

int e1000_send(const void *frame, u32 len)
{
    if (!ok || len > 1514) return -1;
    __asm__ volatile("" ::: "memory");
    txd_t *t = &txd[tx_cur];
    if (!(t->status & 1)) return -1;                   /* кольцо занято */
    const u8 *s = frame;
    for (u32 i = 0; i < len; i++) txbuf[tx_cur][i] = s[i];
    t->len = (u16)len;
    t->status = 0;
    t->cmd = 1 | 2 | 8;                                /* EOP, IFCS, RS */
    tx_cur = (tx_cur + 1) % NTX;
    wr(R_TDT, tx_cur);
    return 0;
}

void e1000_poll(void (*rx)(const u8 *, u32))
{
    if (!ok) return;
    for (u32 n = 0; n < NRX; n++) {
        __asm__ volatile("" ::: "memory");
        rxd_t *r = &rxd[rx_cur];
        if (!(r->status & 1)) break;                   /* DD */
        if ((r->status & 2) && !r->errors && r->len >= 14) rx(rxbuf[rx_cur], r->len);
        r->status = 0;
        wr(R_RDT, rx_cur);
        rx_cur = (rx_cur + 1) % NRX;
    }
}
