#ifndef TTY_H
#define TTY_H
#include <stdint.h>
#include <stddef.h>

void tty_clear(void);
void tty_putc(char c);
void tty_puts(const char* s);
void tty_puts_nf(const char* s);
void tty_put_hex(uint32_t v);
void tty_set_color(uint8_t c);
void tty_backspace(void);
void tty_flush(void);
void tty_erase_cell(void);

#endif
