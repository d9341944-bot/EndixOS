/* Хост-тест логики драйвера JMicron на подставном "железе": регистры в памяти, SMI отвечает сразу, карта сама забирает кадры. */
extern int printf(const char *, ...); extern int puts(const char *);
extern void *mmap(void *, unsigned long, int, int, int, long);
#define MAP_FAILED ((void *)-1)
#define ENDIX_IO_H
#include "kernel/types.h"
static inline void outb(u16 p, u8 v) { (void)p; (void)v; }
#include "drivers/jme.c"
extern int memcmp(const void *, const void *, unsigned long);
#define uintptr_t unsigned long
u32 pit_ticks(void) { static u32 t; return t += 60; }
extern void *memcpy(void *, const void *, unsigned long); extern void *memset(void *, int, unsigned long);
static u8 *regs;
u32 pci_read32(u32 b, u32 d, u32 f, u32 o) { (void)b; (void)d; (void)f; return o == 0x10 ? (u32)(uintptr_t)regs : 0; }
void pci_write32(u32 b, u32 d, u32 f, u32 o, u32 v) { (void)b; (void)d; (void)f; (void)o; (void)v; }
int pci_find(u16 v, u16 dv, u32 *b, u32 *d, u32 *f) { *b = *d = *f = 0; return v == 0x197B && dv == 0x0260; }
void pci_enable(u32 b, u32 d, u32 f) { (void)b; (void)d; (void)f; }
static int got, fails;
static u8 last[2048]; static u32 lastlen;
static void rxcb(const u8 *f, u32 l) { got++; lastlen = l; memcpy(last, f, l); }
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
int main(void)
{
    regs = mmap((void *)0x10000000, 0x1000, 3, 0x02 | 0x20 | 0x40 | 0x10, -1, 0);
    if (regs == MAP_FAILED) return 2;
    /* EEPROM уже загружен: MAC в RXUMA; карта "в сети": 100 Мбит, полный дуплекс, согласование завершено */
    *(u32 *)(regs + 0x38) = 0x44332210; *(u32 *)(regs + 0x3C) = 0x6655;
    *(u32 *)(regs + 0x430) = PHY_LINK_UP | PHY_LINK_AUTONEG_COMPLETE | PHY_LINK_SPEEDDPU_RESOLVED | PHY_LINK_SPEED_100M | PHY_LINK_DUPLEX;
    CHECK(jme_init() == 1, "карта найдена");
    u8 m[6]; jme_mac(m);
    CHECK(m[0] == 0x10 && m[3] == 0x44 && m[5] == 0x66, "MAC прочитан из RXUMA");
    CHECK(!jme_link(), "до опроса линка связи нет");
    jme_poll(rxcb);
    CHECK(jme_link(), "после опроса линк поднят");
    CHECK((*(u32 *)(regs + 0x54) & 0x30) == GHC_SPEED_100M && (*(u32 *)(regs + 0x54) & GHC_DPX), "GHC: 100M, полный дуплекс");
    CHECK(*(u32 *)(regs + 0x2C) == NRX && *(u32 *)(regs + 0x0C) == NTX, "кольца заданы (RXQDC, TXQDC)");
    CHECK((*(u32 *)(regs + 0x20) & 1) && (*(u32 *)(regs + 0x00) & 1), "RX и TX включены");
    CHECK(*(u32 *)(regs + 0x24) == (u32)(uintptr_t)rxd, "RXDBA = адрес кольца");
    for (u32 i = 0; i < NRX; i++) if (!(rxd[i][0] >> 24 & 0x80) || rxd[i][1] != RXBUF || rxd[i][3] != (u32)(uintptr_t)rxbuf[i]) { puts("FAIL  rx descriptor"); fails++; break; }
    /* кадр приходит: карта пишет в буфер 10 байт prepad + 60 байт кадра и результат в дескриптор */
    memset(rxbuf[0], 0, 80); for (int i = 0; i < 60; i++) rxbuf[0][10 + i] = (u8)(i + 1);
    rxd[0][0] = 0x00000000; rxd[0][1] = 70 | (0u << 16) | (0x81u << 24);       /* OWN снят, framesize 70, desccnt 1 + WBCPL */
    jme_poll(rxcb);
    CHECK(got == 1 && lastlen == 60 && last[0] == 1 && last[59] == 60, "приём кадра: prepad отрезан");
    CHECK((rxd[0][0] >> 24 & 0x80) && rxd[0][1] == RXBUF, "дескриптор снова отдан карте");
    /* ошибочный кадр (CRC) */
    rxd[1][0] = 0; rxd[1][1] = 70 | (1u << 16) | (0x81u << 24);
    jme_poll(rxcb);
    CHECK(got == 1 && n_drop == 1, "кадр с ошибкой отброшен");
    CHECK((rxd[1][0] >> 24 & 0x80), "его дескриптор очищен");
    /* отправка */
    u8 fr[100]; for (int i = 0; i < 100; i++) fr[i] = (u8)(i ^ 0x5A);
    CHECK(jme_send(fr, 100) == 0, "отправка принята");
    CHECK((txd[0][0] >> 24) == 0xC0 && (txd[0][2] & 0xFFFF) == 100, "заголовок: OWN|INT, pktsize");
    CHECK((txd[1][0] >> 24) == 0x80 && (txd[1][1] & 0xFFFF) == 100 && txd[1][3] == (u32)(uintptr_t)txbuf[0], "данные: OWN, длина, адрес буфера");
    CHECK(memcmp(txbuf[0], fr, 100) == 0, "кадр скопирован");
    CHECK((*(u32 *)(regs + 0x00) & TXCS_QUEUE0S), "запущена очередь 0");
    CHECK(jme_send(fr, 100) == -1 && n_txfail == 0 ? 0 : 1, "второй кадр идёт во вторую пару дескрипторов");
    /* карта забрала первый кадр: снимает OWN у заголовка */
    txd[0][0] &= 0x00FFFFFF;
    for (int i = 0; i < 20; i++) { fr[0] = (u8)i; jme_send(fr, 64); txd[(tx_cur + NTX - 2) % NTX][0] &= 0x00FFFFFF; }
    CHECK(tx_cur < NTX && n_tx >= 5, "кольцо передачи прокручивается");
    /* линк пропал */
    *(u32 *)(regs + 0x430) = 0; last_chk = 0; jme_poll(rxcb); jme_poll(rxcb);
    CHECK(!jme_link(), "линк пропал: связь выключена");
    char dg[300]; jme_diag(dg, 300); printf("%s", dg);
    printf("fails=%d\n", fails); return fails;
}
