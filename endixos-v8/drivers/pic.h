#ifndef ENDIX_PIC_H
#define ENDIX_PIC_H

void pic_init(void);                 /* ремап на векторы 0x20..0x2F, все IRQ замаскированы */
void pic_unmask(int irq);
void pic_eoi(int irq);
int  pic_is_spurious(int irq);       /* 1 - ложное IRQ7/IRQ15, EOI слать не надо */

#endif
