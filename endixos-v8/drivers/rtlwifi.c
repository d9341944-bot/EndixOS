#include "rtlwifi.h"
#include "pci.h"
#include "../kernel/io.h"
#include "rtl8192c_tables.h"

/* регистры RTL8192C (смещения от BAR2) */
#define REG_SYS_ISO_CTRL   0x0000
#define REG_SYS_FUNC_EN    0x0002
#define REG_APS_FSMCO      0x0004
#define REG_9346CR         0x000A
#define REG_SPS0_CTRL      0x0011
#define REG_RSV_CTRL       0x001C
#define REG_AFE_XTAL_CTRL  0x0024
#define REG_SYS_CFG        0x00F0
#define REG_CR             0x0100
#define REG_MACID          0x0610
#define REG_MCUFWDL        0x0080
#define REG_PBP            0x0104
#define REG_TRXFF_BNDY     0x0114
#define REG_LLT_INIT       0x01E0
#define REG_RQPN           0x0200
#define REG_TDECTRL        0x0208
#define REG_RQPN_NPQ       0x0214
#define REG_TXPKTBUF_BCNQ  0x0424
#define REG_TXPKTBUF_MGQ   0x0425
#define REG_RX_DRVINFO_SZ  0x060F
#define FW_START           0x1000                    /* окно загрузки прошивки во внутренней памяти чипа */
#define FW_PAGE            4096
#define FW_MAX_PAGES       5
#define FW_HDR             32

#define TRP_VAUX_EN        (1u << 23)
#define TYPE_ID            (1u << 27)            /* 1 - 92C (2 антенны), 0 - 88C (1 антенна) */
#define VENDOR_ID          (1u << 19)            /* 1 - UMC */

static u32 st_rx, st_crc, st_bad, st_tx;
static u32 bus, dev, fn, bar, devid, revid;
static int found, usable;

#ifdef RTLWIFI_HOST
static u32 rd32(u32 o) { return sim_rd(o, 4); }
static u32 rd16(u32 o) { return sim_rd(o, 2); }
static u32 rd8(u32 o)  { return sim_rd(o, 1); }
static void wr32(u32 o, u32 v) { sim_wr(o, v, 4); }
static void wr16(u32 o, u32 v) { sim_wr(o, v, 2); }
static void wr8(u32 o, u32 v)  { sim_wr(o, v, 1); }
static void udelay(u32 us) { (void)us; }
#else
static volatile u8 *mm;
static u32 rd32(u32 o) { return *(volatile u32 *)(mm + o); }
static u32 rd16(u32 o) { return *(volatile u16 *)(mm + o); }
static u32 rd8(u32 o)  { return *(volatile u8 *)(mm + o); }
static void wr32(u32 o, u32 v) { *(volatile u32 *)(mm + o) = v; }
static void wr16(u32 o, u32 v) { *(volatile u16 *)(mm + o) = (u16)v; }
static void wr8(u32 o, u32 v)  { *(volatile u8 *)(mm + o) = (u8)v; }
static void udelay(u32 us) { for (u32 i = 0; i < us; i++) io_wait(); }      /* io_wait ~1 мкс */
#endif

int rtlwifi_present(void) { return found; }
const char *rtlwifi_name(void) { return devid == 0x8176 ? "Realtek RTL8188CE" : devid == 0x8177 ? "Realtek RTL8191CE" : devid == 0x8178 ? "Realtek RTL8192CE" : "Realtek RTL8188CE"; }

int rtlwifi_probe(void)
{
    static const u16 ids[] = { 0x8176, 0x8177, 0x8178, 0x8191 };
    found = usable = 0;
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        if (!pci_find(0x10EC, ids[i], &bus, &dev, &fn)) continue;
        found = 1; devid = ids[i];
        revid = pci_read32(bus, dev, fn, 0x08) & 0xFF;
        u32 b = pci_read32(bus, dev, fn, 0x18);                  /* BAR2 - окно регистров */
        if (b & 1) return 0;                                     /* порт ввода-вывода, не память */
        if ((b & 6) == 4 && pci_read32(bus, dev, fn, 0x1C) != 0) return 0;     /* выше 4 ГБ: 32-битная система не достанет */
        bar = b & ~0xFu;
        if (!bar) return 0;
        pci_enable(bus, dev, fn);
#ifndef RTLWIFI_HOST
        mm = (volatile u8 *)(unsigned long)bar;
#endif
        usable = 1;
        return 1;
    }
    return 0;
}

int rtlwifi_mac(u8 mac[6])
{
    if (!usable) return -1;
    int zero = 1, ff = 1;
    for (int i = 0; i < 6; i++) { mac[i] = (u8)rd8(REG_MACID + (u32)i); if (mac[i]) zero = 0; if (mac[i] != 0xFF) ff = 0; }
    return (zero || ff) ? -1 : 0;
}

/* Начало _rtl92ce_init_mac из драйвера Linux: выйти из режима экономии питания и включить MAC. */
int rtlwifi_poweron(void)
{
    if (!usable) return -1;
    wr8(REG_RSV_CTRL, 0x00);
    wr8(REG_SPS0_CTRL, 0x2b);
    wr8(REG_AFE_XTAL_CTRL, 0x0F);
    u32 t = rd8(REG_APS_FSMCO + 1) | 1u;
    udelay(2); wr8(REG_APS_FSMCO + 1, t); udelay(2);
    t = rd8(REG_APS_FSMCO + 1);
    for (int retry = 0; (t & 1u) && retry < 1000; retry++) { udelay(100); t = rd8(REG_APS_FSMCO + 1); }
    if (t & 1u) return -2;                                       /* чип не вышел из режима ожидания */
    wr16(REG_APS_FSMCO, 0x1012);
    wr8(REG_SYS_ISO_CTRL + 1, 0x82);
    udelay(2);
    wr16(REG_CR, 0x2ff);                                         /* включить блоки MAC, DMA, передачу и приём */
    udelay(100);
    return (rd16(REG_CR) & 0x2ff) == 0x2ff ? 0 : -3;
}

/* Таблица связей буфера передачи (LLT): нужна до загрузки прошивки. Конфигурация как в Linux (LLT_CONFIG 5). */
static int llt_write(u32 addr, u32 data)
{
    wr32(REG_LLT_INIT, ((addr & 0xFF) << 8) | (data & 0xFF) | (1u << 30));
    for (int i = 0; i < 20; i++) { if (((rd32(REG_LLT_INIT) >> 30) & 3) == 0) return 0; udelay(10); }
    return -1;
}

int rtlwifi_llt_init(void)
{
    if (!usable) return -1;
    const u32 maxpage = 255, bndy = 246;
    wr16(REG_RQPN_NPQ, 0x0000);
    wr32(REG_RQPN, 0x80b01c29);
    wr32(REG_TRXFF_BNDY, 0x27FF0000u | bndy);
    wr8(REG_TDECTRL + 1, bndy);
    wr8(REG_TXPKTBUF_BCNQ, bndy);
    wr8(REG_TXPKTBUF_MGQ, bndy);
    wr8(0x45D, bndy);
    wr8(REG_PBP, 0x11);
    wr8(REG_RX_DRVINFO_SZ, 0x4);
    for (u32 i = 0; i < bndy - 1; i++) if (llt_write(i, i + 1)) return -3;
    if (llt_write(bndy - 1, 0xFF)) return -3;
    for (u32 i = bndy; i < maxpage; i++) if (llt_write(i, i + 1)) return -3;
    if (llt_write(maxpage, bndy)) return -3;
    return 0;
}

static const u8 *fwblob; static u32 fwlen;
void rtlwifi_set_blob(const u8 *p, u32 n) { fwblob = p; fwlen = n; }
const u8 *rtlwifi_blob(u32 *n) { if (n) *n = fwlen; return fwblob; }
int rtlwifi_have_blob(void) { return fwblob && fwlen; }
int rtlwifi_fw_start(void) { return rtlwifi_fw_load(fwblob, fwlen); }
static u32 fw_ver;
u32 rtlwifi_fw_version(void) { return fw_ver; }

/* Загрузка прошивки (rtl8192cfw.bin из linux-firmware) в чип и запуск. 0 - прошивка работает. */
int rtlwifi_fw_load(const u8 *fw, u32 len)
{
    if (!usable) return -1;
    if (!fw || len < FW_HDR + 64) return -2;
    u32 sig = (u32)fw[0] | ((u32)fw[1] << 8);
    if ((sig & 0xFFF0) == 0x92C0 || (sig & 0xFFF0) == 0x88C0) {          /* есть заголовок 32 байта */
        fw_ver = (u32)fw[4] | ((u32)fw[5] << 8);
        fw += FW_HDR; len -= FW_HDR;
    } else fw_ver = 0;
    u32 padded = (len + 3) & ~3u;
    if (padded > FW_PAGE * FW_MAX_PAGES) return -2;
    /* включить режим загрузки */
    wr8(REG_SYS_FUNC_EN + 1, rd8(REG_SYS_FUNC_EN + 1) | 0x04);
    wr8(REG_MCUFWDL, rd8(REG_MCUFWDL) | 0x01);
    wr8(REG_MCUFWDL + 2, rd8(REG_MCUFWDL + 2) & 0xF7);
    for (u32 page = 0, off = 0; off < padded; page++, off += FW_PAGE) {
        wr8(REG_MCUFWDL + 2, (rd8(REG_MCUFWDL + 2) & 0xF8) | (page & 7));
        u32 n = padded - off < FW_PAGE ? padded - off : FW_PAGE;
        for (u32 i = 0; i < n; i++) wr8(FW_START + i, off + i < len ? fw[off + i] : 0);
    }
    wr8(REG_MCUFWDL, rd8(REG_MCUFWDL) & 0xFE);
    wr8(REG_MCUFWDL + 1, 0x00);
    /* ждать контрольную сумму, потом команду запуска и готовность */
    u32 v = 0; int ok = 0;
    for (int i = 0; i < 100 && !ok; i++) { v = rd32(REG_MCUFWDL); if (v & 4u) ok = 1; else udelay(100); }
    if (!ok) return -4;
    v = (v | 2u) & ~0x40u;
    wr32(REG_MCUFWDL, v);
    for (int i = 0; i < 100; i++) { if (rd32(REG_MCUFWDL) & 0x40u) return 0; udelay(5000); }
    return -5;
}

/* ---- этап 3a: MAC, baseband, AGC и радио-таблицы (после прошивки) ---- */
#define BB_RFMOD     0x800
#define BB_RFINTFS   0x870
#define BB_HSSI2_A   0x824
#define BB_HSSI2_B   0x82C
#define BB_3WIRE_A   0x840
#define BB_3WIRE_B   0x844
#define BB_OE_A      0x860
#define BB_OE_B      0x864

static void tdelay(u32 addr)                                      /* псевдо-адреса таблиц - паузы */
{
    u32 us = addr == 0xfe ? 50000 : addr == 0xfd ? 5000 : addr == 0xfc ? 1000 : addr == 0xfb ? 50 : addr == 0xfa ? 5 : 1;
    udelay(us);
}
static int is_delay(u32 a) { return a >= 0xf9 && a <= 0xfe; }

static void bb_set(u32 reg, u32 mask, u32 data)
{
    if (mask != 0xFFFFFFFFu) {
        u32 sh = 0; while (!((mask >> sh) & 1)) sh++;
        data = (rd32(reg) & ~mask) | (data << sh);
    }
    wr32(reg, data);
}
static u32 bb_get(u32 reg, u32 mask)
{
    u32 sh = 0; while (!((mask >> sh) & 1)) sh++;
    return (rd32(reg) & mask) >> sh;
}
static void rf_write(int path, u32 off, u32 data)                  /* 3-проводная шина радио */
{
    wr32(path ? BB_3WIRE_B : BB_3WIRE_A, (((off & 0x3f) << 20) | (data & 0xFFFFF)) & 0x0FFFFFFFu);
}

static int phy_is92c(void) { return (rd32(REG_SYS_CFG) & TYPE_ID) != 0; }

int rtlwifi_phy_config(void)
{
    if (!usable) return -1;
    int t2 = phy_is92c();
    /* MAC */
    for (unsigned i = 0; i + 1 < sizeof t_mac / 4; i += 2) wr8(t_mac[i], t_mac[i + 1]);
    if (t2) wr8(0x14, 0x71); else wr8(0x04CA, 0x0A);
    /* baseband: тактирование и сброс блоков */
    wr16(REG_SYS_FUNC_EN, rd16(REG_SYS_FUNC_EN) | (1u << 13) | 3u);
    wr8(0x28, 0x83); wr8(0x29, 0xdb);
    wr8(0x1F, 0x07);                                               /* RF_EN | RF_RSTB | RF_SDMRSTB */
    wr8(REG_SYS_FUNC_EN, 0xE3);
    wr8(REG_AFE_XTAL_CTRL + 1, 0x80);
    wr32(0x4C, rd32(0x4C) | (1u << 23));
    const u32 *ph = t2 ? t_phy2t : t_phy1t; unsigned pn = t2 ? sizeof t_phy2t / 4 : sizeof t_phy1t / 4;
    for (unsigned i = 0; i + 1 < pn; i += 2) { if (is_delay(ph[i])) tdelay(ph[i]); else { wr32(ph[i], ph[i + 1]); udelay(1); } }
    const u32 *ag = t2 ? t_agc2t : t_agc1t; unsigned an = t2 ? sizeof t_agc2t / 4 : sizeof t_agc1t / 4;
    for (unsigned i = 0; i + 1 < an; i += 2) { wr32(ag[i], ag[i + 1]); udelay(1); }
    /* радио: путь A (и B у 92C) */
    for (int path = 0; path < (t2 ? 2 : 1); path++) {
        u32 sw = path ? (0x10u << 16) : 0x10u, saved = bb_get(BB_RFINTFS, sw);
        bb_set(path ? BB_OE_B : BB_OE_A, 0x10u << 16, 1); udelay(1);
        bb_set(path ? BB_OE_B : BB_OE_A, 0x10u, 1); udelay(1);
        bb_set(path ? BB_HSSI2_B : BB_HSSI2_A, 0x400, 0); udelay(1);
        bb_set(path ? BB_HSSI2_B : BB_HSSI2_A, 0x800, 0); udelay(1);
        const u32 *r = path ? t_rb2t : (t2 ? t_ra2t : t_ra1t);
        unsigned rn = path ? sizeof t_rb2t / 4 : (t2 ? sizeof t_ra2t / 4 : sizeof t_ra1t / 4);
        for (unsigned i = 0; i + 1 < rn; i += 2) { if (is_delay(r[i])) tdelay(r[i]); else { rf_write(path, r[i], r[i + 1]); udelay(1); } }
        bb_set(BB_RFINTFS, sw, saved);
    }
    bb_set(BB_RFMOD, 0x1000000, 1);                                /* CCK */
    bb_set(BB_RFMOD, 0x2000000, 1);                                /* OFDM */
    return 0;
}

/* ---- этап 3b/4: кольца DMA, приём и передача кадров 802.11, смена канала ---- */
#define TXD 64                                              /* размер дескриптора передачи */
#define RXD 32                                              /* размер дескриптора приёма */
#define NTX 8
#define NRX 16
#define RXBUF 2816
#define TXBUF 1700
#define QUEUE_BE 1
#define QUEUE_MGNT 6
#define RCR_APPFCS    (1u << 31)
#define RCR_APPPHYST  (1u << 28)
#define RCR_HTCLOC    (1u << 14)
#define RCR_AMF       (1u << 13)
#define RCR_ADF       (1u << 11)
#define RCR_ACF       (1u << 12)
#define RCR_AICV      (1u << 9)
#define RCR_ACRC32    (1u << 8)
#define RCR_AB        (1u << 3)
#define RCR_AM        (1u << 2)
#define RCR_APM       (1u << 1)
#define RCR_BASE      (RCR_APPFCS | RCR_APPPHYST | RCR_HTCLOC | RCR_AMF | RCR_ACF | RCR_ADF | RCR_AICV | RCR_ACRC32 | RCR_AB | RCR_AM | RCR_APM)

static u32 txr_mgnt[NTX * TXD / 4] __attribute__((aligned(256)));
static u32 txr_be[NTX * TXD / 4] __attribute__((aligned(256)));
static u32 txr_dummy[2 * TXD / 4] __attribute__((aligned(256)));
static u32 rxr[NRX * RXD / 4] __attribute__((aligned(256)));
static u8 txb[2][NTX][TXBUF] __attribute__((aligned(8)));
static u8 rxb[NRX][RXBUF] __attribute__((aligned(8)));
static int tx_idx[2], rx_idx, started, cur_chan;
static u32 chnlval[2];
#define PHYADDR(p) ((u32)(unsigned long)(p))
#define MB() __asm__ volatile("" ::: "memory")

static void tx_ring_init(u32 *ring, int n)
{
    for (int i = 0; i < n * TXD / 4; i++) ring[i] = 0;
    for (int i = 0; i < n; i++) ring[i * (TXD / 4) + 10] = PHYADDR(ring + ((i + 1) % n) * (TXD / 4));       /* слово 10 - адрес следующего дескриптора */
}
static void rx_arm(int i)
{
    volatile u32 *d = rxr + i * (RXD / 4);
    d[6] = PHYADDR(rxb[i]);
    MB();
    d[0] = (RXBUF & 0x3FFF) | (i == NRX - 1 ? 1u << 30 : 0) | (1u << 31);         /* длина буфера, EOR на последнем, владелец - чип */
}

/* Вторая часть _rtl92ce_init_mac из Linux: DMA, фильтры приёма, адреса колец. Вызывается до загрузки прошивки. */
int rtlwifi_dma_init(void)
{
    if (!usable) return -1;
    tx_ring_init(txr_mgnt, NTX); tx_ring_init(txr_be, NTX); tx_ring_init(txr_dummy, 2);
    for (int i = 0; i < NRX; i++) rx_arm(i);
    tx_idx[0] = tx_idx[1] = rx_idx = 0;
    wr32(0x0124, 0xFFFFFFFFu); wr8(0x012C, 0xFF);                  /* сбросить статусы прерываний */
    wr16(REG_TRXFF_BNDY + 2, 0x27FF);
    wr16(0x010C, (rd16(0x010C) & 0xF) | 0xF771);
    wr8(0x0420 + 1, 0x1F);
    wr32(0x0608, RCR_BASE);
    wr32(0x0604, (1u << 9) | (1u << 12) | (1u << 13));
    wr8(0x04D0, 0);
    wr32(0x0308, PHYADDR(txr_dummy)); wr32(0x0310, PHYADDR(txr_dummy));
    wr32(0x0318, PHYADDR(txr_mgnt));
    wr32(0x0320, PHYADDR(txr_dummy)); wr32(0x0328, PHYADDR(txr_dummy));
    wr32(0x0330, PHYADDR(txr_be));
    wr32(0x0338, PHYADDR(txr_dummy));
    wr32(0x0340, PHYADDR(rxr));
    wr8(0x0300 + 3, (rd32(REG_SYS_CFG) & TYPE_ID) ? 0x77 : 0x22);
    wr32(0x0304, 0);
    wr8(0x0600, rd8(0x0600) & ~(1u << 6));
    for (int i = 0; i < 200 && (rd8(0x0600) & 0x80); i++) udelay(10);
    wr32(0x01C0, 0);
    return 0;
}

int rtlwifi_mac_get(u8 mac[6])
{
    if (!usable) return -1;
    u32 sum = 0, ff = 1;
    for (int i = 0; i < 6; i++) { mac[i] = (u8)rd8(REG_MACID + i); sum |= mac[i]; if (mac[i] != 0xFF) ff = 0; }
    if (sum == 0 || ff || (mac[0] & 1)) {                          /* автозагрузка не дала адрес: свой, локально администрируемый */
        u32 lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        mac[0] = 0x02; mac[1] = 0xE4; mac[2] = (u8)(lo >> 24); mac[3] = (u8)(lo >> 16 ^ hi); mac[4] = (u8)(lo >> 8); mac[5] = (u8)lo;
        for (int i = 0; i < 6; i++) wr8(REG_MACID + i, mac[i]);
    }
    return 0;
}

void rtlwifi_set_channel(int ch)
{
    if (!usable || ch < 1 || ch > 14) return;
    for (int p = 0; p < (phy_is92c() ? 2 : 1); p++) {
        chnlval[p] = (chnlval[p] & 0xfffffc00u) | (u32)ch;
        rf_write(p, 0x18, chnlval[p]);
    }
    cur_chan = ch;
    udelay(10000);
}

void rtlwifi_set_bssid(const u8 *b)
{
    if (!usable) return;
    for (int i = 0; i < 6; i++) wr8(0x0618 + i, b ? b[i] : 0);
}

void rtlwifi_set_link(int up)
{
    if (!usable) return;
    wr8(REG_CR + 2, (rd8(REG_CR + 2) & 0xFC) | (up ? 2u : 0u));    /* MSR: инфраструктурная сеть / нет связи */
}

static int rate_for(const u8 *f)
{
    int type = (f[0] >> 2) & 3;
    return type == 0 || (f[4] & 1) ? 4 : 6;                        /* управление и широковещание - 6 Мбит/с OFDM, данные - 12 */
}

void rtlwifi_tx80211(const u8 *f, u32 n)
{
    if (!started || n < 10 || n > TXBUF - 8) return;
    st_tx++;
    int mgmt = ((f[0] >> 2) & 3) == 0, q = mgmt ? 0 : 1;
    u32 *ring = mgmt ? txr_mgnt : txr_be;
    int i = tx_idx[q];
    volatile u32 *d = ring + i * (TXD / 4);
    if (d[0] & (1u << 31)) return;                                 /* кольцо занято */
    u8 *buf = txb[q][i];
    for (u32 k = 0; k < n; k++) buf[k] = f[k];
    u32 next = d[10];
    for (int k = 0; k < TXD / 4; k++) d[k] = 0;
    d[10] = next;
    u32 seq = (u32)(f[22] | f[23] << 8) >> 4;
    d[7] = n; d[8] = PHYADDR(buf);
    d[1] = ((mgmt ? 0x12u : 0x00u) << 8) | (7u << 16);             /* очередь, RATE_ID */
    d[3] = (seq & 0xFFF) << 16;
    d[4] = 1u << 8;                                                /* скорость задана явно */
    d[5] = (u32)rate_for(f) | (0x1Fu << 8) | (0xFu << 13);
    MB();
    d[0] = n | (32u << 16) | ((f[4] & 1) ? 1u << 24 : 0) | (1u << 26) | (1u << 27);
    MB();
    d[0] |= 1u << 31;
    MB();
    tx_idx[q] = (i + 1) % NTX;
    wr16(0x0300, 1u << (mgmt ? QUEUE_MGNT : QUEUE_BE));            /* разбудить очередь */
}

static int rssi_of(const u8 *info, u32 mcs)
{
    int dbm;
    if (mcs <= 3) {                                                /* CCK */
        int agc = info[5], lna = (agc & 0xE0) >> 5, vga = agc & 0x1F;
        switch (lna) {
        case 7: dbm = vga <= 27 ? -100 + 2 * (27 - vga) : -100; break;
        case 6: dbm = -48 + 2 * (2 - vga); break;
        case 5: dbm = -42 + 2 * (7 - vga); break;
        case 4: dbm = -36 + 2 * (7 - vga); break;
        case 3: dbm = -24 + 2 * (7 - vga); break;
        case 2: dbm = -12 + 2 * (5 - vga); break;
        case 1: dbm = 8 - 2 * vga; break;
        default: dbm = 14 - 2 * vga; break;
        }
        dbm += 6;
    } else dbm = (info[0] & 0x3F) * 2 - 110;
    return dbm < -100 ? -100 : dbm > -20 ? -20 : dbm;
}

void rtlwifi_rxstat(u32 *v) { v[0] = st_rx; v[1] = st_crc; v[2] = st_bad; v[3] = st_tx; v[4] = started ? rd32(0x0608) : 0; v[5] = started ? rd8(0x0016) : 0; }
void rtlwifi_poll(void (*rx)(const u8 *, u32, int))
{
    if (!started) return;
    for (int n = 0; n < NRX; n++) {
        volatile u32 *d = rxr + rx_idx * (RXD / 4);
        u32 w0 = d[0];
        if (w0 & (1u << 31)) break;                                /* этот буфер ещё у чипа */
        u32 len = w0 & 0x3FFF, drv = ((w0 >> 16) & 15) * 8, shift = (w0 >> 24) & 3, mcs = d[3] & 0x3F;
        const u8 *b = rxb[rx_idx];
        st_rx++; if (w0 & 0x4000) st_crc++; else if (!(len > 14 && drv + shift + len <= RXBUF)) st_bad++;
        if (!(w0 & 0x4000) && len > 14 && drv + shift + len <= RXBUF && rx)
            rx(b + drv + shift, len - 4, (w0 >> 26) & 1 ? rssi_of(b, mcs) : -70);       /* len включает FCS, он отрезается */
        rx_arm(rx_idx);
        rx_idx = (rx_idx + 1) % NRX;
    }
}

static void bb_txpower(u32 idx)
{
    u32 v = idx * 0x01010101u;
    wr32(0x0E00, v); wr32(0x0E04, v); wr32(0x0E10, v); wr32(0x0E14, v); wr32(0x0E18, v); wr32(0x0E1C, v);
    bb_set(0x0E08, 0xFF00, idx); bb_set(0x086C, 0xFFFFFF00u, idx * 0x010101u);
}

/* Полный запуск: питание, буфер, DMA, прошивка, таблицы, приём и передача. 0 - готово. */
int rtlwifi_start(void)
{
    if (!usable) return -1;
    if (started) return 0;
    if (!fwblob || !fwlen) return -12;
    if (rtlwifi_poweron()) return -10;
    if (rtlwifi_llt_init()) return -11;
    rtlwifi_dma_init();
    int r = rtlwifi_fw_load(fwblob, fwlen);
    if (r == -4 || r == -2) return -13;
    if (r) return -14;
    if (rtlwifi_phy_config()) return -15;
    wr32(0x0608, (rd32(0x0608) & ~(RCR_ACRC32 | (1u << 30) | (1u << 29))) | RCR_AICV | RCR_APPFCS | RCR_APPPHYST);   /* без APP_MIC/APP_ICV: чип не дописывает лишние байты */
    wr8(0x0480, 0x08); wr8(0x0603, 0x04);                          /* hw_configure: 20 МГц, скорости, интервалы */
    wr32(0x0440, 0x0FFF); wr8(0x051B, 0x09); wr8(0x045C, 0);
    wr16(0x0420, 0x1F80); wr16(0x042A, 0x0707); wr32(0x04CC, 0x02012802);
    wr8(0x0423, 0xFF);
    wr32(0x0430, 0x01000000); wr32(0x0434, 0x07060504); wr32(0x0438, 0x01000000); wr32(0x043C, 0x07060504);
    wr32(0x0458, 0xb972a841); wr8(0x055A, 2); wr8(0x055D, 0xff); wr8(0x0550, 0x1d);
    wr8(0x0422, rd8(0x0422) & ~(1u << 6)); wr8(0x0541, 0x64); wr8(0x0542, rd8(0x0542) & ~1u); wr8(0x0511, 0x66);
    wr32(0x0670, 0xC0000000u); wr8(0x0680, 0);                      /* таблица ключей чипа пуста, шифрование делает программа */
    wr16(0x06A0, 0xFFFF); wr16(0x06A2, 0); wr16(0x06A4, 0xFFFF);
    wr32(0x0120, 0); wr32(0x0128, 0);                              /* без прерываний: кольца опрашиваются */
    /* антенна, полоса 20 МГц, мощность */
    if (phy_is92c()) bb_set(BB_OE_B, 0x60, 1); else bb_set(BB_OE_A, 0x300, 2);
    bb_set(BB_RFMOD, 1, 0); bb_set(0x0900, 1, 0); bb_set(0x0884, 1u << 10, 1);
    const u32 *ra = phy_is92c() ? t_ra2t : t_ra1t; unsigned ran = phy_is92c() ? sizeof t_ra2t / 4 : sizeof t_ra1t / 4;
    chnlval[0] = chnlval[1] = 0;
    for (unsigned i = 0; i + 1 < ran; i += 2) if (ra[i] == 0x18) chnlval[0] = ra[i + 1];
    for (unsigned i = 0; i + 1 < sizeof t_rb2t / 4; i += 2) if (t_rb2t[i] == 0x18) chnlval[1] = t_rb2t[i + 1];
    chnlval[0] = (chnlval[0] & 0xfffff3ffu) | 0x400; chnlval[1] = (chnlval[1] & 0xfffff3ffu) | 0x400;
    bb_txpower(0x28);
    started = 1;
    rtlwifi_set_channel(1);
    return 0;
}
int rtlwifi_started(void) { return started; }

static char *put(char *p, const char *e, const char *s) { while (*s && p < e) *p++ = *s++; return p; }
static char *hexn(char *p, const char *e, u32 v, int digits)
{
    static const char x[] = "0123456789abcdef";
    for (int i = digits - 1; i >= 0 && p < e; i--) *p++ = x[(v >> (i * 4)) & 15];
    return p;
}

void rtlwifi_diag(char *buf, int n)
{
    char *p = buf, *e = buf + n - 1;
    if (!found) { p = put(p, e, "No Realtek RTL8188CE/8192CE Wi-Fi adapter found on the PCI bus.\n"); *p = 0; return; }
    p = put(p, e, "Wi-Fi: Realtek "); p = put(p, e, devid == 0x8176 ? "RTL8188CE" : devid == 0x8177 ? "RTL8191CE" : devid == 0x8178 ? "RTL8192CE" : "RTL8188CE (8191)");
    p = put(p, e, " 10ec:"); p = hexn(p, e, devid, 4); p = put(p, e, " rev "); p = hexn(p, e, revid, 2);
    p = put(p, e, " at pci "); p = hexn(p, e, bus, 2); p = put(p, e, ":"); p = hexn(p, e, dev, 2); p = put(p, e, "."); p = hexn(p, e, fn, 1);
    p = put(p, e, "\n");
    if (!usable) { p = put(p, e, "  registers are not reachable (BAR2 is not 32-bit memory)\n"); *p = 0; return; }
    p = put(p, e, "  BAR2 0x"); p = hexn(p, e, bar, 8); p = put(p, e, "\n");
    u32 cfg = rd32(REG_SYS_CFG);
    p = put(p, e, "  SYS_CFG 0x"); p = hexn(p, e, cfg, 8);
    p = put(p, e, (cfg & TYPE_ID) ? " (92C, 2T2R)" : " (88C, 1T1R)");
    p = put(p, e, (cfg & TRP_VAUX_EN) ? " A-chip" : ((cfg & VENDOR_ID) ? " UMC" : " B-chip"));
    p = put(p, e, "\n  APS_FSMCO 0x"); p = hexn(p, e, rd16(REG_APS_FSMCO), 4);
    p = put(p, e, "  SYS_FUNC_EN 0x"); p = hexn(p, e, rd16(REG_SYS_FUNC_EN), 4);
    p = put(p, e, "  CR 0x"); p = hexn(p, e, rd16(REG_CR), 4);
    p = put(p, e, "  9346CR 0x"); p = hexn(p, e, rd8(REG_9346CR), 2);
    u8 mac[6]; int m = rtlwifi_mac(mac);
    p = put(p, e, "\n  MAC ");
    for (int i = 0; i < 6; i++) { p = hexn(p, e, rd8(REG_MACID + (u32)i), 2); if (i < 5) p = put(p, e, ":"); }
    p = put(p, e, m == 0 ? "\n" : "  (not loaded yet: try `wifi on`)\n");
    p = put(p, e, "  MCUFWDL 0x"); p = hexn(p, e, rd32(REG_MCUFWDL), 8); p = put(p, e, "\n");
    *p = 0;
}
