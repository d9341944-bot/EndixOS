#ifndef ENDIX_JME_H
#define ENDIX_JME_H
#include "../kernel/types.h"

/* JMicron JMC250/JMC260 (Ethernet нетбука DNS и многих других). Работа опросом, без прерываний.
 * Порядок инициализации и значения регистров взяты из драйвера Linux jme.c (GPL). НЕ проверено на железе. */
int  jme_init(void);                       /* 1 - карта найдена и запущена (кабель может быть ещё не подключён) */
void jme_mac(u8 mac[6]);
int  jme_send(const void *frame, u32 len);
void jme_poll(void (*rx)(const u8 *frame, u32 len));
int  jme_link(void);
void jme_stats(u32 *rx, u32 *tx, u32 *txfail, int *present);
void jme_diag(char *out, int cap);         /* текст для команды nicinfo */
#endif
