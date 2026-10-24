#ifndef ENDIX_IDT_H
#define ENDIX_IDT_H

#include "types.h"

/* Состояние регистров, как его оставляет isr_common (boot/isr.asm). */
typedef struct {
    u32 edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    u32 int_no, err_code;
    u32 eip, cs, eflags;
} regs_t;

typedef void (*irq_handler_t)(void);

void idt_init(void);
void irq_register(int irq, irq_handler_t handler);

#endif
