/* COM1: отладочный вывод. В QEMU: -serial stdio */
#include "serial.h"
#include "../kernel/io.h"

#define COM1 0x3F8

void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* без прерываний */
    outb(COM1 + 3, 0x80);   /* включить DLAB */
    outb(COM1 + 0, 0x01);   /* делитель 1 -> 115200 бод */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8 бит, без чётности, 1 стоп-бит */
    outb(COM1 + 2, 0xC7);   /* FIFO */
    outb(COM1 + 4, 0x0B);
}

void serial_putc(char c)
{
    while (!(inb(COM1 + 5) & 0x20))
        ;
    outb(COM1, (u8)c);
}

void serial_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            serial_putc('\r');
        serial_putc(*s++);
    }
}

void serial_put_hex(u32 v)
{
    static const char digits[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4)
        serial_putc(digits[(v >> i) & 0xF]);
}

void serial_put_dec(u32 v)
{
    char buf[11];
    int i = 0;
    if (v == 0) {
        serial_putc('0');
        return;
    }
    while (v) {
        buf[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i--)
        serial_putc(buf[i]);
}
