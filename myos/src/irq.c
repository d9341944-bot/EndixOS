#include "irq.h"
#include "pic.h"
#include "io.h"

static irq_handler_t handlers[16] = {0};

void irq_install_handler(int irq, irq_handler_t h)  { handlers[irq] = h; }
void irq_uninstall_handler(int irq)                 { handlers[irq] = 0; }

void irq_handler(struct regs* r) {
    int irq = r->int_no - 32;

    if (irq >= 8) outb(PIC2_CMD, 0x20); /* EOI slave */
    outb(PIC1_CMD, 0x20);               /* EOI master */

    if (irq >= 0 && irq < 16 && handlers[irq])
        handlers[irq](r);
}

void irq_init(void) {
    pic_remap(32, 40);
    /* Пока всё замаскируем, конкретные IRQ будем разрешать по мере надобности */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
