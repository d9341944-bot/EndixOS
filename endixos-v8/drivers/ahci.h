#ifndef ENDIX_AHCI_H
#define ENDIX_AHCI_H
#include "../kernel/types.h"

/* Диски SATA через AHCI (режим по умолчанию на современных и многих старых ноутбуках, класс PCI 0106).
 * Опрос без прерываний, DMA в статических буферах (память отображена 1:1, страниц нет). */
typedef struct { int port; u32 sectors; char model[41]; } ahci_disk_t;

int  ahci_init(void);                                   /* число найденных дисков (0 - контроллера или дисков нет) */
int  ahci_disk_count(void);
const ahci_disk_t *ahci_disk(int i);
int  ahci_read(int disk, u32 lba, u32 count, void *buf);          /* count <= 64 секторов за раз; 0 - успех */
int  ahci_write(int disk, u32 lba, u32 count, const void *buf);   /* запись НА НАСТОЯЩИЙ ДИСК: только установщик, после подтверждения */
const char *ahci_status(void);                          /* что нашли или почему нет (для диагностики) */
#endif
