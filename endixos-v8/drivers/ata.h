#ifndef ENDIX_ATA_H
#define ENDIX_ATA_H

#include "../kernel/types.h"

/* Простейший ATA PIO (LBA28), основной IDE-диск. Записывать разрешено только на виртуальный диск QEMU,
 * чтобы на настоящем железе никогда не затронуть чужие данные. */
int  ata_init(void);                          /* 1 - диск есть и в него можно писать настройки */
int  ata_read(u32 lba, void *buf);            /* один сектор 512 байт; 0 - успех */
int  ata_write(u32 lba, const void *buf);

#endif
