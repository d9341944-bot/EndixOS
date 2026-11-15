#ifndef ENDIX_USB_H
#define ENDIX_USB_H
#include "../kernel/types.h"
#include "../kernel/blk.h"

/* USB 2.0 (EHCI): флешки и диски (Bulk-Only + SCSI). Хабы, клавиатуры и мыши (low/full speed) пока не поддерживаются. */
int         usb_scan(void);                 /* пересканирует порты; возвращает число накопителей */
int         usb_drive_count(void);
blk_t      *usb_blk(int i);                 /* блочное устройство (сектора по 512 байт) */
const char *usb_drive_name(int i);
const char *usb_log(void);                  /* текст отчёта последнего сканирования */
#endif
