#ifndef ENDIX_SERIAL_H
#define ENDIX_SERIAL_H

#include "../kernel/types.h"

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);
void serial_put_hex(u32 v);
void serial_put_dec(u32 v);

#endif
