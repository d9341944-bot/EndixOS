#ifndef ENDIX_RESOLUTION_H
#define ENDIX_RESOLUTION_H
#include "../kernel/types.h"
#include "../kernel/blk.h"

/* Выбор разрешения экрана на следующий запуск. Режим задаёт GRUB (gfxpayload), поэтому "на лету" его не сменить:
 * мы переписываем /boot/grub/grub.cfg установленной системы, ставя выбранный режим первым пунктом. */
int  res_count(void);
const char *res_name(int i);
void res_set_installed(int inst);                 /* ядро сообщает: запуск с установленного диска */
int  res_available(void);                         /* 1 - можно сохранять (установленная система) */
int  res_apply(int i, const char **msg);          /* 0 - сохранено */
fat_t *res_mount(void);                           /* система на диске (FAT32, есть /boot/grub/grub.cfg) или 0 */
int  res_current(void);                           /* индекс режима, совпадающего с текущим экраном, или -1 */
#endif
