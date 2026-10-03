#ifndef IRQ_H
#define IRQ_H
#include "isr.h"

typedef void (*irq_handler_t)(struct regs*);

void irq_install_handler(int irq, irq_handler_t h);
void irq_uninstall_handler(int irq);
void irq_handler(struct regs* r);
void irq_init(void);

#endif
