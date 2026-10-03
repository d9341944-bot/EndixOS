#include "serial.h"
#include "io.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);  /* disable interrupts */
    outb(COM1 + 3, 0x80);  /* enable DLAB */
    outb(COM1 + 0, 0x03);  /* divisor low = 3, 38400 baud */
    outb(COM1 + 1, 0x00);  /* divisor high */
    outb(COM1 + 3, 0x03);  /* 8N1 */
    outb(COM1 + 2, 0xC7);  /* enable FIFO, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B);  /* IRQs enabled, RTS/DSR set */
}

static int tx_empty(void) { return inb(COM1 + 5) & 0x20; }

void serial_putc(char c) {
    while (!tx_empty()) { }
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char* s) { while (*s) serial_putc(*s++); }

void serial_put_hex(uint32_t v) {
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t n = (v >> i) & 0xF;
        serial_putc(n < 10 ? '0' + n : 'a' + n - 10);
    }
}
