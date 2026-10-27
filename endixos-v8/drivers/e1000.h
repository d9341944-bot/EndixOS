#ifndef ENDIX_E1000_H
#define ENDIX_E1000_H
#include "../kernel/types.h"

/* Intel e1000 (стандартная сетевая карта QEMU). Работа опросом, без прерываний. */
int  e1000_init(void);                       /* 1 - карта найдена и запущена */
void e1000_mac(u8 mac[6]);
int  e1000_send(const void *frame, u32 len);
void e1000_poll(void (*rx)(const u8 *frame, u32 len));
int  e1000_link(void);
#endif
