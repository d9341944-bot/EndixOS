#include "jme.h"
#include "pci.h"
#include "pit.h"
#include "../kernel/io.h"

/* ---- регистры (jme.h из Linux) ---- */
#define JME_TXCS     0x0000
#define JME_TXDBA_LO 0x0004
#define JME_TXDBA_HI 0x0008
#define JME_TXQDC    0x000C
#define JME_TXNDA    0x0010
#define JME_TXMCS    0x0014
#define JME_TXPFC    0x0018
#define JME_TXTRHD   0x001C
#define JME_RXCS     0x0020
#define JME_RXDBA_LO 0x0024
#define JME_RXDBA_HI 0x0028
#define JME_RXQDC    0x002C
#define JME_RXNDA    0x0030
#define JME_RXMCS    0x0034
#define JME_RXUMA_LO 0x0038
#define JME_RXUMA_HI 0x003C
#define JME_RXMCHT_LO 0x0040
#define JME_RXMCHT_HI 0x0044
#define JME_SMI      0x0050
#define JME_GHC      0x0054
#define JME_PMCS     0x0060
#define JME_PHY_PWR  0x0424
#define JME_PHY_LINK 0x0430
#define JME_SMBCSR   0x0440
#define JME_GPREG0   0x0808
#define JME_GPREG1   0x080C
#define JME_IEVE     0x0820
#define JME_IENC     0x082C
#define JME_CHIPMODE 0x0844

#define TXCS_ENABLE 0x1u
#define TXCS_BURST 0x4u
#define TXCS_QUEUE0S 0x100u
#define TXCS_DEFAULT TXCS_BURST
#define TXMCS_PADDING 0x1u
#define TXMCS_CRC 0x2u
#define TXMCS_COLLISION 0x4u
#define TXMCS_CARRIERSENSE 0x8u
#define TXMCS_BACKOFF 0x10u
#define TXMCS_DEFER 0x20u
#define TXMCS_TTHOLD_FULL 0x300u
#define TXMCS_IFG1_16_8 0x20000000u
#define TXMCS_IFG2_8_5 0x40000000u
#define TXMCS_DEFAULT (TXMCS_IFG2_8_5 | TXMCS_IFG1_16_8 | TXMCS_TTHOLD_FULL | TXMCS_DEFER | TXMCS_CRC | TXMCS_PADDING)
#define TXTRHD_FULLDUPLEX 0u
#define TXTRHD_HALFDUPLEX (0x80000000u | ((0x2000u << 8) & 0x7FFFFF00u) | 0x80u | (8u & 0x7Fu))

#define RXCS_ENABLE 0x1u
#define RXCS_QST 0x4u
#define RXCS_DEFAULT (0x30000000u | 0x03000000u | 0x800u)       /* FIFOTHTP_128T | DMAREQSZ_128B | RETRYCNT_32 */
#define RXMCS_CHECKSUM 0x1u
#define RXMCS_PREPAD 0x2u
#define RXMCS_VTAGRM 0x4u
#define RXMCS_FLOWCTRL 0x8u
#define RXMCS_UNIFRAME 0x100u
#define RXMCS_BRDFRAME 0x400u
#define RXMCS_DEFAULT (RXMCS_VTAGRM | RXMCS_PREPAD | RXMCS_FLOWCTRL | RXMCS_CHECKSUM)

#define GHC_SWRST 0x40000000u
#define GHC_TO_CLK_GPHY 0x00400000u
#define GHC_TXMAC_CLK_GPHY 0x00100000u
#define GHC_DPX 0x40u
#define GHC_SPEED 0x30u
#define GHC_SPEED_10M 0x10u
#define GHC_SPEED_100M 0x20u
#define GHC_SPEED_1000M 0x30u

#define SMI_OP_WRITE 0x20u
#define SMI_OP_REQ 0x10u
#define PHY_LINK_SPEED_MASK 0xC000u
#define PHY_LINK_SPEED_10M 0x0000u
#define PHY_LINK_SPEED_100M 0x4000u
#define PHY_LINK_SPEED_1000M 0x8000u
#define PHY_LINK_DUPLEX 0x2000u
#define PHY_LINK_SPEEDDPU_RESOLVED 0x800u
#define PHY_LINK_UP 0x400u
#define PHY_LINK_AUTONEG_COMPLETE 0x200u
#define SMBCSR_CNACK 0x20000u
#define SMBCSR_RELOAD 0x10000u
#define SMBCSR_EEPROMD 0x20u
#define PMCS_STMASK 0xFFFF0000u
#define PMCS_MFEN 0x1u
#define GPREG0_DEFAULT 0x00300201u                              /* PCIRLMT_4 | PCCTMR_1us | PHYADDR 1 */
#define GPREG1_DEFAULT 0x00020000u                              /* PCREQN */

#define PHY_ID 1
#define MII_BMCR 0
#define MII_BMSR 1
#define MII_ADVERTISE 4

#define NRX 16
#define RXBUF 1600
#define NTX 16                                                  /* дескрипторов; кадр занимает 2 (заголовок + данные) */
#define TXBUF 1536

static volatile u8 *mmio;
static int ok, link_up, chiprev;
static u8 mymac[6];
static u32 reg_ghc, reg_gpreg1 = GPREG1_DEFAULT, cur_phylink, last_chk, phy_bmsr;
static u32 n_rx, n_tx, n_txfail, n_drop;

static u32 rxd[NRX][4] __attribute__((aligned(16)));
static u32 txd[NTX][4] __attribute__((aligned(16)));
static u8  rxbuf[NRX][RXBUF] __attribute__((aligned(16)));
static u8  txbuf[NTX / 2][TXBUF] __attribute__((aligned(16)));
static u32 rx_cur, tx_cur;

static u32 rd(u32 r) { return *(volatile u32 *)(mmio + r); }
static void wr(u32 r, u32 v) { *(volatile u32 *)(mmio + r) = v; }
static void wrf(u32 r, u32 v) { wr(r, v); (void)rd(r); }          /* с обратным чтением: запись дошла */
static inline void barrier(void) { __asm__ volatile("" ::: "memory"); }

/* Прерываний и PIT на этом этапе ещё нет: задержки по обращениям к порту 0x80 (около 1 мкс каждое). */
static void udelay(u32 n) { while (n--) outb(0x80, 0); }
static void mdelay(u32 n) { while (n--) udelay(1000); }

/* ---- MDIO ---- */
static u32 mdio_read(int reg)
{
    for (int again = reg == MII_BMSR ? 1 : 0; ; again--) {
        wr(JME_SMI, SMI_OP_REQ | ((u32)PHY_ID << 6 & 0x7C0u) | ((u32)reg << 11 & 0xF800u));
        barrier();
        u32 v = 0; int i;
        for (i = 5000; i > 0; i--) { udelay(20); v = rd(JME_SMI); if (!(v & SMI_OP_REQ)) break; }
        if (!i) return 0;
        if (again <= 0) return v >> 16;
    }
}
static void mdio_write(int reg, u32 val)
{
    wr(JME_SMI, SMI_OP_WRITE | SMI_OP_REQ | (val << 16 & 0xFFFF0000u) | ((u32)PHY_ID << 6 & 0x7C0u) | ((u32)reg << 11 & 0xF800u));
    barrier();
    for (int i = 5000; i > 0; i--) { udelay(20); if (!(rd(JME_SMI) & SMI_OP_REQ)) break; }
}

/* ---- сброс MAC ---- */
static void clk_on(void)  { reg_ghc |= GHC_TO_CLK_GPHY | GHC_TXMAC_CLK_GPHY; wr(JME_GHC, reg_ghc); }
static void clk_off(void) { reg_ghc &= ~(GHC_TO_CLK_GPHY | GHC_TXMAC_CLK_GPHY); wr(JME_GHC, reg_ghc); }

static void reset_mac(void)
{
    reg_ghc &= ~(GHC_SPEED | GHC_DPX); wr(JME_GHC, reg_ghc);
    clk_on(); udelay(1);
    wr(JME_GHC, reg_ghc | GHC_SWRST); udelay(1);                   /* assert reset */
    clk_off(); udelay(1);
    wr(JME_GHC, reg_ghc); udelay(1);                               /* clear reset */
    clk_on(); udelay(1);
    clk_off();
    wr(JME_RXDBA_LO, 0); wr(JME_RXDBA_HI, 0); wr(JME_RXQDC, 0); wr(JME_RXNDA, 0);
    wr(JME_TXDBA_LO, 0); wr(JME_TXDBA_HI, 0); wr(JME_TXQDC, 0); wr(JME_TXNDA, 0);
    wr(JME_RXMCHT_LO, 0); wr(JME_RXMCHT_HI, 0);
    wr(JME_GPREG0, GPREG0_DEFAULT);
}

static int reload_eeprom(void)
{
    u32 v = rd(JME_SMBCSR);
    if (v & SMBCSR_EEPROMD) {
        v |= SMBCSR_CNACK; wr(JME_SMBCSR, v);
        v |= SMBCSR_RELOAD; wr(JME_SMBCSR, v);
        mdelay(12);
        int i;
        for (i = 2000; i > 0; i--) { mdelay(1); if (!(rd(JME_SMBCSR) & SMBCSR_RELOAD)) break; }
        if (!i) return -1;
    }
    return 0;
}

/* ---- кольца ---- */
static void rx_clean(u32 i)
{
    u32 *d = rxd[i];
    d[0] = 0; d[1] = RXBUF; d[2] = 0; d[3] = (u32)(unsigned long)rxbuf[i];
    barrier();
    d[0] = (0x80u | 0x40u) << 24;                                  /* OWN | INT */
}

static void rings_init(void)
{
    for (u32 i = 0; i < NRX; i++) rx_clean(i);
    for (u32 i = 0; i < NTX; i++) { txd[i][0] = txd[i][1] = txd[i][2] = txd[i][3] = 0; }
    rx_cur = 0; tx_cur = 0;
}

static void rx_start(void)
{
    wr(JME_RXCS, RXCS_DEFAULT);                                    /* очередь 0 */
    barrier();
    wr(JME_RXDBA_LO, (u32)(unsigned long)rxd); wr(JME_RXDBA_HI, 0);
    wr(JME_RXNDA, (u32)(unsigned long)rxd);
    wr(JME_RXQDC, NRX);
    u32 m = mymac[3] << 24 | mymac[2] << 16 | mymac[1] << 8 | mymac[0];
    wr(JME_RXUMA_LO, m);
    wr(JME_RXUMA_HI, mymac[5] << 8 | mymac[4]);
    wr(JME_RXMCS, RXMCS_DEFAULT | RXMCS_BRDFRAME | RXMCS_UNIFRAME);
    barrier();
    wrf(JME_RXCS, RXCS_DEFAULT | RXCS_ENABLE | RXCS_QST);
    clk_on();
}
static void rx_stop(void)
{
    wr(JME_RXCS, RXCS_DEFAULT);
    barrier();
    for (int i = 10; i > 0 && (rd(JME_RXCS) & RXCS_ENABLE); i--) mdelay(1);
    clk_off();
}
static void tx_start(void)
{
    wr(JME_TXCS, TXCS_DEFAULT);
    barrier();
    wr(JME_TXDBA_LO, (u32)(unsigned long)txd); wr(JME_TXDBA_HI, 0);
    wr(JME_TXNDA, (u32)(unsigned long)txd);
    wr(JME_TXQDC, NTX);
    barrier();
    wrf(JME_TXCS, TXCS_DEFAULT | TXCS_ENABLE);
    clk_on();
}
static void tx_stop(void)
{
    wr(JME_TXCS, TXCS_DEFAULT);
    barrier();
    for (int i = 10; i > 0 && (rd(JME_TXCS) & TXCS_ENABLE); i--) mdelay(1);
    clk_off();
}

/* ---- связь ---- */
static void phy_reset(void)
{
    u32 bmcr = mdio_read(MII_BMCR);
    mdio_write(MII_BMCR, bmcr & ~0x0800u);                          /* выключить power down */
    if (((chiprev & 0xF) >= 5)) wr(JME_PHY_PWR, rd(JME_PHY_PWR) & ~(0x01000000u | 0x02000000u | 0x04000000u | 0x08000000u));
    mdio_write(MII_ADVERTISE, 0x0DE1u);                             /* 10/100, полный и половинный дуплекс, pause */
    mdio_write(MII_BMCR, mdio_read(MII_BMCR) | 0x8000u);            /* reset */
    for (int i = 0; i < 200; i++) { mdelay(1); if (!(mdio_read(MII_BMCR) & 0x8000u)) break; }
    mdio_write(MII_BMCR, 0x1200u);                                  /* автосогласование: включить и перезапустить */
}

static void link_changed(u32 ph)
{
    tx_stop(); rx_stop();
    reset_mac();
    rings_init();
    link_up = 0;
    cur_phylink = ph;
    if (!(ph & PHY_LINK_UP)) return;
    switch (ph & PHY_LINK_SPEED_MASK) {
    case PHY_LINK_SPEED_10M:  reg_ghc |= GHC_SPEED_10M; break;
    case PHY_LINK_SPEED_100M: reg_ghc |= GHC_SPEED_100M; break;
    default:                  reg_ghc |= GHC_SPEED_1000M; break;
    }
    if (ph & PHY_LINK_DUPLEX) {
        wr(JME_TXMCS, TXMCS_DEFAULT); wr(JME_TXTRHD, TXTRHD_FULLDUPLEX);
        reg_ghc |= GHC_DPX;
    } else {
        wr(JME_TXMCS, TXMCS_DEFAULT | TXMCS_BACKOFF | TXMCS_CARRIERSENSE | TXMCS_COLLISION);
        wr(JME_TXTRHD, TXTRHD_HALFDUPLEX);
    }
    wr(JME_GHC, reg_ghc);
    wr(JME_GPREG1, reg_gpreg1);
    rx_start();
    tx_start();
    link_up = 1;
}

static void link_check(void)
{
    u32 ph = rd(JME_PHY_LINK);
    if (ph & PHY_LINK_UP) {
        if (!(ph & PHY_LINK_AUTONEG_COMPLETE)) {                    /* линк без согласования: скорость из BMCR */
            u32 b = mdio_read(MII_BMCR);
            ph = PHY_LINK_UP | ((b & 0x2000u) ? PHY_LINK_SPEED_100M : PHY_LINK_SPEED_10M) | ((b & 0x100u) ? PHY_LINK_DUPLEX : 0);
        } else {
            for (int i = 1000; i > 0 && !(ph & PHY_LINK_SPEEDDPU_RESOLVED); i--) { udelay(1); ph = rd(JME_PHY_LINK); }
        }
    }
    u32 key = ph & (PHY_LINK_UP | PHY_LINK_SPEED_MASK | PHY_LINK_DUPLEX);
    if (key != (cur_phylink & (PHY_LINK_UP | PHY_LINK_SPEED_MASK | PHY_LINK_DUPLEX)) || (!link_up && (ph & PHY_LINK_UP)))
        link_changed(ph);
}

/* ---- интерфейс ---- */
int jme_init(void)
{
    u32 b = 0, d = 0, f = 0;
    if (!pci_find(0x197B, 0x0260, &b, &d, &f) && !pci_find(0x197B, 0x0250, &b, &d, &f)) return 0;
    pci_enable(b, d, f);
    u32 bar0 = pci_read32(b, d, f, 0x10);
    if (bar0 & 1) return 0;
    mmio = (volatile u8 *)(bar0 & ~0xFu);
    chiprev = (int)((rd(JME_CHIPMODE) >> 8) & 0xFF);

    wr(JME_IENC, 0xFFFFFFFFu);                                      /* без прерываний */
    wr(JME_IEVE, 0xFFFFFFFFu);
    wr(JME_PMCS, PMCS_STMASK | PMCS_MFEN);                          /* сбросить статусы питания */
    reload_eeprom();                                                /* MAC-адрес загружается из EEPROM в RXUMA */
    u32 lo = rd(JME_RXUMA_LO), hi = rd(JME_RXUMA_HI);
    mymac[0] = (u8)lo; mymac[1] = (u8)(lo >> 8); mymac[2] = (u8)(lo >> 16); mymac[3] = (u8)(lo >> 24);
    mymac[4] = (u8)hi; mymac[5] = (u8)(hi >> 8);
    if ((mymac[0] | mymac[1] | mymac[2] | mymac[3] | mymac[4] | mymac[5]) == 0 || (mymac[0] & 1)) {   /* EEPROM пуст: локальный адрес */
        mymac[0] = 0x02; mymac[1] = 0x4A; mymac[2] = 0x4D; mymac[3] = 0x45; mymac[4] = 0x26; mymac[5] = 0x01;
    }
    reset_mac();
    phy_reset();
    rings_init();
    ok = 1;
    return 1;
}

void jme_mac(u8 mac[6]) { for (int i = 0; i < 6; i++) mac[i] = mymac[i]; }
int  jme_link(void) { return ok && link_up; }

int jme_send(const void *frame, u32 len)
{
    if (!ok || !link_up || len > 1514) return -1;
    barrier();
    u32 *h = txd[tx_cur], *dd = txd[(tx_cur + 1) % NTX];
    if (h[0] >> 24 & 0x80u) { n_txfail++; return -1; }              /* заголовок ещё у карты */
    u8 *buf = txbuf[tx_cur / 2];
    const u8 *s = frame;
    for (u32 i = 0; i < len; i++) buf[i] = s[i];
    dd[0] = 0; dd[1] = len & 0xFFFFu; dd[2] = 0; dd[3] = (u32)(unsigned long)buf;
    barrier();
    dd[0] = 0x80u << 24;                                            /* OWN у данных */
    h[0] = 0; h[1] = 0; h[2] = len & 0xFFFFu; h[3] = 0;             /* pktsize */
    barrier();
    h[0] = (0x80u | 0x40u) << 24;                                   /* OWN | INT у заголовка - последним */
    barrier();
    tx_cur = (tx_cur + 2) % NTX;
    wrf(JME_TXCS, TXCS_DEFAULT | TXCS_QUEUE0S | TXCS_ENABLE);
    n_tx++;
    return 0;
}

void jme_poll(void (*rx)(const u8 *, u32))
{
    if (!ok) return;
    u32 t = pit_ticks();
    if ((i32)(t - last_chk) >= 50 || last_chk == 0) { last_chk = t | 1u; link_check(); }
    if (!link_up) return;
    for (u32 n = 0; n < NRX; n++) {
        barrier();
        u32 *d = rxd[rx_cur];
        u32 w0 = d[0], w1 = d[1];
        if ((w0 & 0x80000000u) || !(w1 >> 24 & 0x80u)) break;      /* OWN у карты или запись результата ещё не завершена */
        u32 cnt = w1 >> 24 & 0x7Fu, err = w1 >> 16 & 0xFFu, fs = w1 & 0xFFFFu;
        if (cnt > 1 || err) {
            n_drop++;
            for (u32 k = 0; k < cnt && k < NRX; k++) rx_clean((rx_cur + k) % NRX);
            rx_cur = (rx_cur + (cnt ? cnt : 1)) % NRX;
            continue;
        }
        if (fs >= 10 + 14 && fs <= RXBUF) { n_rx++; rx(rxbuf[rx_cur] + 10, fs - 10); }
        rx_clean(rx_cur);
        rx_cur = (rx_cur + 1) % NRX;
    }
}

void jme_stats(u32 *rx, u32 *tx, u32 *txfail, int *present) { *rx = n_rx; *tx = n_tx; *txfail = n_txfail; *present = ok; }

static char *hx32(char *p, u32 v) { static const char h[] = "0123456789abcdef"; for (int s = 28; s >= 0; s -= 4) *p++ = h[v >> s & 15]; return p; }
static char *str(char *p, const char *s) { while (*s) *p++ = *s++; return p; }

void jme_diag(char *out, int cap)
{
    char *p = out;
    if (!ok) { p = str(p, "JMicron JMC250/260: not found\n"); *p = 0; return; }
    p = str(p, "JMicron NIC: BAR0 "); p = hx32(p, (u32)(unsigned long)mmio);
    p = str(p, "  chiprev "); p = hx32(p, (u32)chiprev);
    p = str(p, "\nPHY_LINK "); p = hx32(p, rd(JME_PHY_LINK));
    p = str(p, "  BMSR "); p = hx32(p, mdio_read(MII_BMSR));
    p = str(p, "  BMCR "); p = hx32(p, mdio_read(MII_BMCR));
    p = str(p, "\nGHC "); p = hx32(p, rd(JME_GHC));
    p = str(p, "  RXCS "); p = hx32(p, rd(JME_RXCS));
    p = str(p, "  TXCS "); p = hx32(p, rd(JME_TXCS));
    p = str(p, "\nlink "); p = str(p, link_up ? "up" : "down");
    p = str(p, "  rx "); p = hx32(p, n_rx); p = str(p, " tx "); p = hx32(p, n_tx);
    p = str(p, " drop "); p = hx32(p, n_drop); p = str(p, " txfail "); p = hx32(p, n_txfail);
    p = str(p, "\n");
    (void)phy_bmsr;
    *p = 0; (void)cap;
}
