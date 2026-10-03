#ifndef PIC_H
#define PIC_H
#include <stdint.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

void pic_remap(int offset1, int offset2);
void pic_set_mask(uint8_t irq);
void pic_clear_mask(uint8_t irq);

#endif
