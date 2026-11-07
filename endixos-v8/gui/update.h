#ifndef ENDIX_UPDATE_H
#define ENDIX_UPDATE_H
#include "../kernel/types.h"

/* Software Update: проверка и установка новых версий EndixOS по сети (HTTPS).
 * Манифест update/latest.txt (build=, version=, size=, sha256=, notes=) и ядро update/endixos.elf лежат в репозитории на GitHub.
 * Установка только на установленной системе: ядро записывается в /boot/endixos.elf, старое остаётся как /boot/previous.elf
 * и добавляется отдельным пунктом в меню GRUB. Цельность проверяет SHA-256 из манифеста (подлинность сервера TLS пока НЕ проверяется). */
enum { UP_IDLE, UP_CHECKING, UP_UPTODATE, UP_AVAILABLE, UP_DOWNLOADING, UP_INSTALLING, UP_READY, UP_ERROR };

void update_init(void);
void update_check(void);                 /* начать проверку (если не занято) */
void update_install(void);               /* скачать и установить найденное обновление */
void update_tick(u32 now);               /* вызывать часто (PIT_HZ = 100) */
int  update_state(void);
int  update_percent(void);               /* 0..100 при скачивании и установке */
const char *update_message(void);        /* строка ошибки или состояния */
const char *update_version(void);        /* версия найденного обновления, например "8.1" */
int  update_build(void);
const char *update_notes(void);          /* "пункт|пункт|пункт" */
u32  update_size(void);
int  update_checked_secs(u32 now);       /* сколько секунд назад была успешная проверка, -1 - ещё не проверяли */
int  update_can_install(void);           /* 1 - запущены с установленной системы */
int  update_auto(void);
void update_set_auto(int on);
void update_restart(void);

/* для тестов */
int  update_parse(const char *text);     /* разбор манифеста: 0 - успех */
#endif
