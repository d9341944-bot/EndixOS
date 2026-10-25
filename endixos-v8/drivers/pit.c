/* Системный таймер 8253/8254, канал 0, PIT_HZ тиков в секунду. */
#include "pit.h"
#include "pic.h"
#include "../kernel/io.h"
#include "../kernel/idt.h"

static volatile u32 ticks;

static void pit_irq(void)
{
    ticks++;
}

void pit_init(void)
{
    u32 divisor = 1193182 / PIT_HZ;
    outb(0x43, 0x36);                    /* канал 0, lo/hi, режим 3 */
    outb(0x40, (u8)(divisor & 0xFF));
    outb(0x40, (u8)(divisor >> 8));
    irq_register(0, pit_irq);
    pic_unmask(0);
}

u32 pit_ticks(void)
{
    return ticks;
}
