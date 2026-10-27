#ifndef ENDIX_PREFS_H
#define ENDIX_PREFS_H

#include "../kernel/types.h"

/* Сохранение настроек на диск: обои, тема, акцент, Dock, Liquid Glass, виджеты. Блок 512 байт в секторе 0
 * виртуального диска; пишется, только если сектор пустой или уже наш. Без диска ничего не делает. */
typedef int (*prefs_rd_t)(u32 lba, void *buf);
typedef int (*prefs_wr_t)(u32 lba, const void *buf);

void prefs_init(prefs_rd_t rd, prefs_wr_t wr);    /* читает и применяет сохранённое */
void prefs_tick(u32 ticks);                       /* сохраняет, когда настройки изменились и успокоились */

#endif
