#include "isr.h"
#include "tty.h"

static const char* exc_names[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "BOUND Range", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection", "Page Fault", "Reserved",
    "x87 FPU Error", "Alignment Check", "Machine Check", "SIMD FPU Error",
    "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor", "VMM Communication", "Security", "Reserved"
};

static uint32_t read_cr2(void) {
    uint32_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

void isr_handler(struct regs* r) {
    tty_set_color(0x0C);
    tty_puts("\n\n*** KERNEL PANIC ***\n");
    tty_puts("Exception: ");
    if (r->int_no < 32) tty_puts(exc_names[r->int_no]);
    else                tty_puts("Unknown");
    tty_puts(" ("); tty_put_hex(r->int_no); tty_puts(")\n");

    if (r->int_no == 14) {
        tty_puts("faulting addr (CR2) = "); tty_put_hex(read_cr2()); tty_putc('\n');
        tty_puts("error code bits: ");
        if (r->err_code & 1) tty_puts("P "); else tty_puts("- ");
        if (r->err_code & 2) tty_puts("W "); else tty_puts("R ");
        if (r->err_code & 4) tty_puts("U "); else tty_puts("S ");
        tty_putc('\n');
    }

    tty_puts("err_code = "); tty_put_hex(r->err_code); tty_putc('\n');
    tty_puts("eip      = "); tty_put_hex(r->eip);      tty_putc('\n');
    tty_puts("cs       = "); tty_put_hex(r->cs);       tty_putc('\n');
    tty_puts("eflags   = "); tty_put_hex(r->eflags);   tty_putc('\n');
    tty_puts("eax="); tty_put_hex(r->eax);
    tty_puts(" ebx="); tty_put_hex(r->ebx);
    tty_puts(" ecx="); tty_put_hex(r->ecx);
    tty_puts(" edx="); tty_put_hex(r->edx); tty_putc('\n');
    tty_puts("esi="); tty_put_hex(r->esi);
    tty_puts(" edi="); tty_put_hex(r->edi);
    tty_puts(" ebp="); tty_put_hex(r->ebp);
    tty_puts(" esp="); tty_put_hex(r->esp); tty_putc('\n');

    tty_puts("\nSystem halted. Reset QEMU to continue.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

extern void irq_handler(struct regs* r);

void isr_dispatch(struct regs* r) {
    if (r->int_no < 32)         isr_handler(r);
    else if (r->int_no < 48)    irq_handler(r);
    else if (r->int_no == 0x80) {
        extern void syscall_handler(struct regs* r);
        syscall_handler(r);
    }
}
