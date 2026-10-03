#include "pit.h"
#include "io.h"

#define PIT_CH0  0x40
#define PIT_CMD  0x43
#define PIT_FREQ 1193182

void pit_init(uint32_t freq) {
    uint32_t div = PIT_FREQ / freq;
    outb(PIT_CMD, 0x36); /* channel 0, lo/hi, mode 3 (square), binary */
    outb(PIT_CH0, div & 0xFF);
    outb(PIT_CH0, (div >> 8) & 0xFF);
}
