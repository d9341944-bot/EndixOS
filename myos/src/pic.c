#include "pic.h"
#include "io.h"

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

void pic_remap(int o1, int o2) {
    uint8_t m1 = inb(PIC1_DATA);
    uint8_t m2 = inb(PIC2_DATA);

    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC1_DATA, o1);                    io_wait();
    outb(PIC2_DATA, o2);                    io_wait();
    outb(PIC1_DATA, 4);                     io_wait(); /* slave on IRQ2 */
    outb(PIC2_DATA, 2);                     io_wait(); /* slave identity */
    outb(PIC1_DATA, ICW4_8086);             io_wait();
    outb(PIC2_DATA, ICW4_8086);             io_wait();

    outb(PIC1_DATA, m1);
    outb(PIC2_DATA, m2);
}

void pic_set_mask(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = irq < 8 ? irq : irq - 8;
    outb(port, inb(port) | (1 << bit));
}

void pic_clear_mask(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = irq < 8 ? irq : irq - 8;
    outb(port, inb(port) & ~(1 << bit));
}
