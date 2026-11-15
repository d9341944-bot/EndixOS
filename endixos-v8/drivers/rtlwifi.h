#ifndef ENDIX_RTLWIFI_H
#define ENDIX_RTLWIFI_H
#include "../kernel/types.h"

/* Wi-Fi Realtek RTL8188CE / RTL8192CE (PCIe, 10EC:8176/8177/8178/8191).
 * Этап 2: таблица LLT и загрузка прошивки (файл rtl8192cfw.bin подаётся GRUB-модулем). Этап 1: поиск на шине PCI, чтение регистров, включение питания чипа, MAC-адрес.
 * Регистры сверены с описанием в драйвере Linux rtlwifi (rtl8192ce/reg.h, hw.c). */
int  rtlwifi_probe(void);                        /* 1 - адаптер найден и память доступна */
int  rtlwifi_present(void);
const char *rtlwifi_name(void);
void rtlwifi_diag(char *buf, int n);             /* текст для команды `wifi` */
int  rtlwifi_poweron(void);                      /* 0 - питание включено (проверка по регистрам) */
int  rtlwifi_mac(u8 mac[6]);                     /* 0 - в регистрах MAC не нули и не 0xFF */

int  rtlwifi_llt_init(void);                     /* 0 - таблица буфера передачи записана */
int  rtlwifi_fw_load(const u8 *fw, u32 len);     /* 0 - прошивка запущена; -2 файл, -4 нет контрольной суммы, -5 не стартовала */
u32  rtlwifi_fw_version(void);
void rtlwifi_set_blob(const u8 *p, u32 n);       /* ядро передаёт сюда GRUB-модуль rtl8192cfw.bin */
int  rtlwifi_have_blob(void);
const u8 *rtlwifi_blob(u32 *len);                /* файл прошивки, пришедший модулем GRUB, или 0 */
int  rtlwifi_fw_start(void);
int  rtlwifi_phy_config(void);
int  rtlwifi_dma_init(void);
int  rtlwifi_start(void);                        /* полный запуск; -10 питание, -11 буфер, -12 нет файла прошивки, -13/-14 прошивка, -15 таблицы */
int  rtlwifi_started(void);
int  rtlwifi_mac_get(u8 mac[6]);              /* адрес из чипа или созданный и записанный */
void rtlwifi_set_channel(int ch);
void rtlwifi_set_bssid(const u8 *b);             /* 0 - сбросить */
void rtlwifi_set_link(int up);
void rtlwifi_tx80211(const u8 *f, u32 n);
void rtlwifi_poll(void (*rx)(const u8 *f, u32 n, int rssi));
                   /* этап 3a: таблицы MAC/BB/AGC/радио; 0 - записано */

#ifdef RTLWIFI_HOST
extern u32 sim_rd(u32 off, int size);            /* модель контроллера в тесте */
extern void sim_wr(u32 off, u32 v, int size);
void rtlwifi_rxstat(u32 *v);   /* 0 кадров принято, 1 с ошибкой CRC, 2 битых, 3 отправлено, 4 RCR, 5 - */
#endif
#endif
