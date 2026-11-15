#ifndef ENDIX_STA_H
#define ENDIX_STA_H
#include "types.h"

/* Станция Wi-Fi (клиент 802.11, WPA2-PSK/CCMP или открытая сеть): сканирование, подключение, EAPOL-рукопожатие,
 * шифрование CCMP, перевод кадров Ethernet <-> 802.11. Не зависит от железа: радио подключается через sta_radio_t. */
#define STA_MAX_NETS 16
enum { SEC_OPEN, SEC_WPA2, SEC_UNSUPPORTED };
typedef struct { char ssid[33]; u8 bssid[6]; u8 channel; i8 rssi; u8 sec; } sta_net_t;
enum { STA_IDLE, STA_SCANNING, STA_AUTH, STA_ASSOC, STA_HANDSHAKE, STA_CONNECTED, STA_FAILED };

typedef struct {
    void (*tx)(const u8 *frame, u32 len);          /* кадр 802.11 без FCS */
    void (*set_channel)(int ch);
    void (*set_bssid)(const u8 *bssid);            /* 0 - сбросить */
    void (*set_link)(int up);                      /* 1 - ассоциирован */
} sta_radio_t;

void sta_init(const u8 mac[6], const sta_radio_t *radio);
void sta_set_callbacks(void (*eth_rx)(const u8 *frame, u32 len), void (*changed)(int state));
void sta_scan(void);
int  sta_scanning(void);
int  sta_net_count(void);
const sta_net_t *sta_net(int i);
int  sta_connect(int idx, const char *pass);       /* 0 - начато */
void sta_disconnect(void);
int  sta_state(void);
const char *sta_text(void);                        /* что показать пользователю */
int  sta_connected_net(void);                      /* индекс в списке или -1 */
void sta_rx(const u8 *frame, u32 len, int rssi_dbm);
void sta_tick(u32 ticks);                          /* 100 Гц */
int  sta_send_eth(const void *frame, u32 len);
#endif
