/* Два каскадных контроллера прерываний 8259. */
#include "pic.h"
#include "../kernel/io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

static u8 mask_master = 0xFF;
static u8 mask_slave  = 0xFF;

void pic_init(void)
{
    outb(PIC1_CMD, 0x11); io_wait();     /* ICW1: инициализация, ICW4 нужен */
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();    /* ICW2: векторы master 0x20..0x27 */
    outb(PIC2_DATA, 0x28); io_wait();    /* slave 0x28..0x2F */
    outb(PIC1_DATA, 0x04); io_wait();    /* ICW3: slave подключён к IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();    /* ICW4: режим 8086 */
    outb(PIC2_DATA, 0x01); io_wait();

    mask_master = 0xFF;
    mask_slave  = 0xFF;
    outb(PIC1_DATA, mask_master);
    outb(PIC2_DATA, mask_slave);
}

void pic_unmask(int irq)
{
    if (irq < 8) {
        mask_master = (u8)(mask_master & ~(1u << irq));
    } else {
        mask_slave  = (u8)(mask_slave & ~(1u << (irq - 8)));
        mask_master = (u8)(mask_master & ~(1u << 2));   /* каскадная линия */
        outb(PIC2_DATA, mask_slave);
    }
    outb(PIC1_DATA, mask_master);
}

void pic_eoi(int irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

int pic_is_spurious(int irq)
{
    if (irq == 7) {
        outb(PIC1_CMD, 0x0B);            /* читать ISR */
        return !(inb(PIC1_CMD) & 0x80);
    }
    if (irq == 15) {
        outb(PIC2_CMD, 0x0B);
        if (!(inb(PIC2_CMD) & 0x80)) {
            outb(PIC1_CMD, 0x20);        /* master всё равно получил IRQ2 */
            return 1;
        }
    }
    return 0;
}
