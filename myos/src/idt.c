#include "idt.h"
#include <stdint.h>

struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

extern void idt_load(uint32_t);
extern uint32_t isr_stub_table[32];
extern uint32_t irq_stub_table[16];

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel       = sel;
    idt[num].always0   = 0;
    idt[num].flags     = flags;
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;

    for (int i = 0; i < 256; i++)
        idt_set_gate(i, 0, 0, 0);

    /* 32 CPU exceptions: вектора 0..31 */
    for (int i = 0; i < 32; i++)
        idt_set_gate(i, isr_stub_table[i], 0x08, 0x8E);

    /* 16 IRQ: вектора 32..47 (после pic_remap(32, 40)) */
    for (int i = 0; i < 16; i++)
        idt_set_gate(32 + i, irq_stub_table[i], 0x08, 0x8E);

    extern void isr128(void);
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, 0xEE);  /* DPL=3 */

    idt_load((uint32_t)&idtp);
}
