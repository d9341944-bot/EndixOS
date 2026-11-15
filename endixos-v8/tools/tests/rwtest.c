#define RTLWIFI_HOST 1
#include "drivers/rtlwifi.c"
extern int printf(const char *, ...); extern char *strstr(const char *, const char *);
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
/* модель PCI */
static u16 dev_present; static u32 bar2 = 0xF0000000u, bar3;
int pci_find(u16 v, u16 d, u32 *b, u32 *dv, u32 *f) { if (v == 0x10EC && d == dev_present) { *b = 2; *dv = 0; *f = 0; return 1; } return 0; }
u32 pci_read32(u32 b, u32 d, u32 f, u32 o) { (void)b; (void)d; (void)f; return o == 0x08 ? 0x02800001u : o == 0x18 ? bar2 : o == 0x1C ? bar3 : 0; }
void pci_write32(u32 b, u32 d, u32 f, u32 o, u32 v) { (void)b; (void)d; (void)f; (void)o; (void)v; }
static int enabled; void pci_enable(u32 b, u32 d, u32 f) { (void)b; (void)d; (void)f; enabled = 1; }
void io_wait_stub(void) {}
/* модель регистров */
static u8 reg[0x6000]; static int stuck, fwbytes, nochk, noboot, pagelog;
u32 sim_rd(u32 o, int s) { u32 v = 0; for (int i = 0; i < s; i++) v |= (u32)reg[o + i] << (8 * i); return v; }
void sim_wr(u32 o, u32 v, int s)
{
    for (int i = 0; i < s; i++) reg[o + i] = (u8)(v >> (8 * i));
    if (o <= 5 && o + s > 5 && !stuck) reg[5] &= (u8)~1;
    if (o <= 0x1E3 && o + s > 0x1E3) reg[0x1E3] &= 0x3F;                       /* LLT: операция выполнена */
    if (o >= 0x1000 && o < 0x1000 + 4096 * 5) { fwbytes++; }
    if (o == 0x82) pagelog = reg[0x82] & 7;
    if (o == 0x80 && o + s > 0x80 && !(reg[0x80] & 1) && fwbytes && !nochk) reg[0x80] |= 4;      /* загрузка закончена: контрольная сумма сошлась */
    if (o == 0x80 && s == 4 && (reg[0x80] & 2) && !noboot) reg[0x80] |= 0x40;                       /* запущена: WINTINI_RDY */                       /* APS_FSMCO+1 bit0 сбрасывается: чип проснулся */
}
int main(void)
{
    char t[600]; u8 mac[6];
    CHECK(rtlwifi_probe() == 0 && !rtlwifi_present(), "нет адаптера - не найден");
    rtlwifi_diag(t, sizeof t); CHECK(strstr(t, "No Realtek"), "диагностика без адаптера");
    CHECK(rtlwifi_poweron() == -1, "включить нечего");
    dev_present = 0x8176; reg[0xF0 + 3] = 0x08; reg[0xF0 + 2] = 0x08;                  /* TYPE_ID (бит 27) и VENDOR_ID (бит 19) */
    for (int i = 0; i < 6; i++) reg[0x610 + i] = (u8)(0x00 + i * 0x11 + 0x10);
    CHECK(rtlwifi_probe() == 1 && enabled && rtlwifi_present(), "RTL8188CE найден, память включена");
    rtlwifi_diag(t, sizeof t);
    CHECK(strstr(t, "RTL8188CE") && strstr(t, "10ec:8176") && strstr(t, "rev 01") && strstr(t, "pci 02:00.0"), "диагностика: имя, id, ревизия, адрес");
    CHECK(strstr(t, "BAR2 0xf0000000") && strstr(t, "92C") && strstr(t, "UMC"), "диагностика: BAR2, тип чипа, производитель");
    CHECK(rtlwifi_mac(mac) == 0 && mac[0] == 0x10 && mac[5] == 0x65 && strstr(t, "10:21:32:43:54:65"), "MAC читается");
    reg[0x611] = 0xFF; reg[0x612] = 0xFF; reg[0x613] = 0xFF; reg[0x614] = 0xFF; reg[0x615] = 0xFF; reg[0x610] = 0xFF;
    CHECK(rtlwifi_mac(mac) != 0, "MAC из одних FF - не загружен");
    reg[5] = 1;
    CHECK(rtlwifi_poweron() == 0, "включение питания проходит");
    CHECK(reg[0x100] == 0xff && reg[0x101] == 0x02 && reg[0x01] == 0x82 && reg[0x04] == 0x12 && reg[0x05] == 0x10, "в регистры записаны значения из драйвера Linux");
    stuck = 1; reg[5] = 1; reg[0x100] = 0;
    CHECK(rtlwifi_poweron() == -2, "чип не просыпается - ошибка -2");
    stuck = 0; bar2 = 0x3001; CHECK(rtlwifi_probe() == 0 && !usable, "BAR2 - порт ввода-вывода: отказ");
    bar2 = 0xF0000004u; bar3 = 1; CHECK(rtlwifi_probe() == 0 && !usable, "64-битный BAR выше 4 ГБ: отказ");
    /* этап 2 */
    bar2 = 0xF0000000u; bar3 = 0; rtlwifi_probe();
    reg[5] = 1;
    CHECK(rtlwifi_llt_init() == 0 && reg[0x114] == 246 && reg[0x117] == 0x27 && reg[0x104] == 0x11 && reg[0x60F] == 4 && reg[0x200] == 0x29 && reg[0x203] == 0x80, "LLT: границы буфера и RQPN как в драйвере");
    static u8 fw[9000]; for (int i = 0; i < 9000; i++) fw[i] = (u8)i; fw[0] = 0xC0; fw[1] = 0x92; fw[4] = 0x2A; fw[5] = 0;
    CHECK(rtlwifi_fw_load(fw, 10) == -2, "слишком короткий файл отвергнут");
    fwbytes = 0; reg[0x80] = 0;
    CHECK(rtlwifi_fw_load(fw, 9000) == 0 && rtlwifi_fw_version() == 42, "прошивка с заголовком загружена и запущена, версия 42");
    CHECK(fwbytes == 8968 && reg[0x1000] == fw[32] && reg[0x1000 + 100] == fw[132] && pagelog == 2, "записано 8968 байт без заголовка, 3 страницы");
    nochk = 1; reg[0x80] = 0; fwbytes = 0;
    CHECK(rtlwifi_fw_load(fw, 9000) == -4, "нет контрольной суммы - ошибка -4");
    nochk = 0; noboot = 1; reg[0x80] = 0;
    CHECK(rtlwifi_fw_load(fw, 9000) == -5, "прошивка не стартовала - ошибка -5");
    static u8 big[30000]; big[0] = 0xC0; big[1] = 0x92; noboot = 0;
    CHECK(rtlwifi_fw_load(big, 30000) == -2, "слишком большой файл отвергнут");
    rtlwifi_set_blob(fw, 9000); CHECK(rtlwifi_have_blob(), "модуль GRUB запомнен");
    /* этап 3a */
    reg[0xF0 + 3] = 0x08; reg[5] = 1;
    CHECK(rtlwifi_phy_config() == 0, "таблицы записаны (2T)");
    CHECK(reg[0x14] == 0x71 && reg[0x28] == 0x83 && reg[0x29] == 0xdb && reg[0x1F] == 7 && reg[0x4E] & 0x80, "MAC, PLL и RF_CTRL как в драйвере");
    { int ok = 1; for (unsigned i = 0; i + 1 < sizeof t_mac / 4; i += 2) { int last = 1; for (unsigned j = i + 2; j + 1 < sizeof t_mac / 4; j += 2) if (t_mac[j] == t_mac[i]) last = 0; if (last && t_mac[i] != 0x14 && reg[t_mac[i]] != (u8)t_mac[i + 1]) ok = 0; }
      CHECK(ok, "все регистры из таблицы MAC записаны"); }
    { unsigned n = sizeof t_ra2t / 4, k = n - 2; while (k > 0 && t_ra2t[k] >= 0xf9 && t_ra2t[k] <= 0xfe) k -= 2;
      u32 want = (((t_ra2t[k] & 0x3f) << 20) | (t_ra2t[k + 1] & 0xFFFFF)) & 0x0FFFFFFF; u32 got = reg[0x840] | reg[0x841] << 8 | reg[0x842] << 16 | (u32)reg[0x843] << 24;
      CHECK(got == want, "радио: последняя запись ушла в 3-проводную шину A"); }
    CHECK(reg[0x803] & 3 && (reg[0x803] & 3) == 3, "включены CCK и OFDM");
    reg[0xF0 + 3] = 0x00; reg[5] = 1;
    CHECK(rtlwifi_phy_config() == 0 && reg[0x4CA] == 0x0A, "88C (1T1R): своя ветка MAC");
    /* этап 3b: запуск, передача, приём */
    reg[0xF0 + 3] = 0x08; reg[5] = 1; reg[0x80] = 0; fwbytes = 0; nochk = 0; noboot = 0;
    CHECK(rtlwifi_start() == 0 && rtlwifi_started(), "полный запуск прошёл");
    CHECK(reg[0x60B] & 0x80 && !(reg[0x609] & 1) && (reg[0x609] & 2), "RCR: ACRC32 снят, AICV включён");
    CHECK(((reg[0x4CC + 3] << 24) | (reg[0x4CC + 2] << 16) | (reg[0x4CC + 1] << 8) | reg[0x4CC]) == 0x02012802, "hw_configure записан");
    { u8 fr[40]; for (int i = 0; i < 40; i++) fr[i] = (u8)i; fr[0] = 0x40; fr[1] = 0; fr[4] = 0xFF; fr[22] = 0x30; fr[23] = 0x01;
      reg[0x300] = 0; rtlwifi_tx80211(fr, 40);
      CHECK(txr_mgnt[0] == (40u | 32u << 16 | 1u << 24 | 1u << 26 | 1u << 27 | 1u << 31) && txr_mgnt[7] == 40 && txr_mgnt[5] == (4u | 0x1Fu << 8 | 0xFu << 13) && (txr_mgnt[1] >> 8 & 0x1F) == 0x12 && (txr_mgnt[3] >> 16) == 0x13, "дескриптор управляющего кадра заполнен");
      CHECK(reg[0x300] == 0x40 && !memcmp(txb[0][0], fr, 40), "очередь MGNT разбужена, кадр скопирован");
      fr[0] = 0x08; fr[4] = 2; rtlwifi_tx80211(fr, 40);
      CHECK((txr_be[5] & 0x3F) == 6 && !(txr_be[0] >> 24 & 1) && reg[0x300] == 0x02, "данные идут в очередь BE на 12 Мбит/с");
      rtlwifi_tx80211(fr, 40); txr_mgnt[0] &= 0x7FFFFFFF; }
    { static int got, glen, grssi; static u8 gb[64];
      void cb(const u8 *f, u32 n, int r) { got++; glen = (int)n; grssi = r; for (u32 i = 0; i < n && i < 64; i++) gb[i] = f[i]; }
      for (int i = 0; i < 40; i++) rxb[0][32 + 2 + i] = (u8)(100 + i);
      rxb[0][0] = 0x2A;
      rxr[0] = 44 | (4u << 16) | (2u << 24) | (1u << 26);
      rxr[3] = 4;
      rtlwifi_poll(cb);
      CHECK(got == 1 && glen == 40 && gb[0] == 100 && gb[39] == 139, "принятый кадр передан без FCS, сдвиги учтены");
      CHECK(grssi == (0x2A & 0x3F) * 2 - 110 && (rxr[0] >> 31) == 1, "RSSI посчитан, буфер возвращён чипу");
      rtlwifi_poll(cb); CHECK(got == 1, "пустое кольцо ничего не отдаёт"); }
    return fails;
}
