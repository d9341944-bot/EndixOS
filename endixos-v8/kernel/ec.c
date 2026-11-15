#include "ec.h"
#include "io.h"

#define EC_DATA 0x62
#define EC_CMD  0x66

static int wait(int ibf)                      /* ibf = 1: ждать, пока буфер входа освободится; 0: пока появится байт на выходе */
{
    for (u32 i = 0; i < 100000; i++) {
        u8 st = inb(EC_CMD);
        if (ibf ? !(st & 2) : (st & 1)) return 0;
        outb(0x80, 0);
    }
    return -1;
}

int ec_read(u8 off, u8 *val)
{
    if (inb(EC_CMD) == 0xFF) return -1;       /* на шине ничего нет */
    if (wait(1)) return -1;
    outb(EC_CMD, 0x80);
    if (wait(1)) return -1;
    outb(EC_DATA, off);
    if (wait(0)) return -1;
    *val = inb(EC_DATA);
    return 0;
}

int ec_dump(u8 out[256])
{
    int n = 0;
    for (int i = 0; i < 256; i++) {
        if (ec_read((u8)i, &out[i])) break;
        n++;
    }
    return n;
}
