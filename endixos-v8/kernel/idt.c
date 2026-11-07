/* IDT, диспетчер исключений и аппаратных прерываний. */
#include "idt.h"
#include "io.h"
#include "../drivers/pic.h"
#include "../drivers/serial.h"

typedef struct {
    u16 base_lo;
    u16 sel;
    u8  zero;
    u8  flags;
    u16 base_hi;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    u16 limit;
    u32 base;
} __attribute__((packed)) idt_ptr_t;

extern u32 isr_stub_table[48];

static idt_entry_t   idt[256];
static idt_ptr_t     idt_ptr;
static irq_handler_t irq_handlers[16];

static void idt_set_gate(int n, u32 base)
{
    idt[n].base_lo = (u16)(base & 0xFFFF);
    idt[n].base_hi = (u16)(base >> 16);
    idt[n].sel     = 0x08;
    idt[n].zero    = 0;
    idt[n].flags   = 0x8E;          /* present, ring 0, 32-битный interrupt gate */
}

void idt_init(void)
{
    for (int i = 0; i < 48; i++)
        idt_set_gate(i, isr_stub_table[i]);

    idt_ptr.limit = (u16)(sizeof(idt) - 1);
    idt_ptr.base  = (u32)&idt;
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
}

void irq_register(int irq, irq_handler_t handler)
{
    if (irq >= 0 && irq < 16)
        irq_handlers[irq] = handler;
}

static void exception(regs_t *r)
{
    u32 cr2 = 0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    serial_puts("\n*** CPU EXCEPTION ");
    serial_put_dec(r->int_no);
    serial_puts("  err=");
    serial_put_hex(r->err_code);
    serial_puts("  eip=");
    serial_put_hex(r->eip);
    if (r->int_no == 14) {
        serial_puts("  cr2=");
        serial_put_hex(cr2);
    }
    serial_puts("\nSystem halted.\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}

void isr_handler_c(regs_t *r)
{
    if (r->int_no < 32) {
        exception(r);
        return;
    }

    int irq = (int)r->int_no - 32;
    if (pic_is_spurious(irq))
        return;                      /* ложное прерывание: EOI не нужен */

    if (irq_handlers[irq])
        irq_handlers[irq]();
    pic_eoi(irq);
}
