#ifndef ENDIX_WIFI_H
#define ENDIX_WIFI_H
/* Менеджер Wi-Fi для Настроек: запуск адаптера, список сетей, подключение. */
enum { WIFI_NONE, WIFI_OFF, WIFI_STARTING, WIFI_ON, WIFI_ERROR };
#define WIFI_SHOW 4                                /* сколько сетей показывает страница */
int         wifi_state(void);
const char *wifi_adapter(void);                    /* "Realtek RTL8188CE" или "" */
const char *wifi_message(void);                    /* строка состояния для страницы */
void        wifi_refresh(void);                    /* найти адаптер (один раз) */
void        wifi_set_enabled(int on);              /* включение выполнится в wifi_step */
void        wifi_step(void);                       /* вызывать при перерисовке: делает запланированную работу */
void        wifi_tick(unsigned now);               /* из главного цикла: приём кадров и таймеры */
void        wifi_scan(void);
int         wifi_net_count(void);                  /* уникальные сети, сильные выше */
const char *wifi_net_name(int i);
int         wifi_net_signal(int i);                /* 0..4 */
int         wifi_net_secure(int i);                /* 0 открытая, 1 с паролем, 2 не поддерживается */
int         wifi_net_connected(int i);
int         wifi_connect(int i, const char *pass); /* 0 начато; -2 пароль короче 8 */
void        wifi_disconnect(void);
int         wifi_busy(void);                       /* идёт поиск или подключение */
/* сохранение между перезагрузками (хранит prefs.c) */
void        wifi_pref_get(char *ssid, char *pass, int *on);   /* ssid[33], pass[64] */
void        wifi_pref_set(const char *ssid, const char *pass, int on);
void        wifi_known_get(int i, char *ssid, char *pass);    /* i = 0..2: ещё запомненные сети (кроме последней) */
void        wifi_known_set(int i, const char *ssid, const char *pass);
void        prefs_flush(void);                                 /* prefs.c: записать настройки на диск немедленно */
#endif
