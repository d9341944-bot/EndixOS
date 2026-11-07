#include "ata.h"
#include "../kernel/io.h"

#define DATA 0x1F0
#define ERR  0x1F1
#define SECT 0x1F2
#define LBAL 0x1F3
#define LBAM 0x1F4
#define LBAH 0x1F5
#define DRV  0x1F6
#define STAT 0x1F7
#define CMD  0x1F7
#define CTRL 0x3F6

static int present;

static int wait_ready(int need_drq)
{
    for (u32 i = 0; i < 1000000; i++) {
        u8 s = inb(STAT);
        if (s == 0xFF) return -1;
        if (s & 0x01 || s & 0x20) return -1;                 /* ERR или DF */
        if (!(s & 0x80) && (!need_drq || (s & 0x08))) return 0;
    }
    return -1;
}

int ata_init(void)
{
    present = 0;
    outb(CTRL, 0x02);                         /* без прерываний: работаем опросом */
    outb(DRV, 0xA0);
    for (int i = 0; i < 4; i++) inb(STAT);
    if (inb(STAT) == 0xFF) return 0;          /* контроллера нет */
    outb(SECT, 0); outb(LBAL, 0); outb(LBAM, 0); outb(LBAH, 0);
    outb(CMD, 0xEC);                          /* IDENTIFY */
    if (inb(STAT) == 0) return 0;
    for (u32 i = 0; i < 1000000 && (inb(STAT) & 0x80); i++) { }
    if (inb(LBAM) || inb(LBAH)) return 0;     /* ATAPI и прочее нам не нужно */
    if (wait_ready(1) < 0) return 0;
    u16 id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(DATA);
    char model[41];
    for (int i = 0; i < 20; i++) { model[i * 2] = (char)(id[27 + i] >> 8); model[i * 2 + 1] = (char)(id[27 + i] & 0xFF); }
    model[40] = 0;
    present = model[0] == 'Q' && model[1] == 'E' && model[2] == 'M' && model[3] == 'U';   /* только виртуальный диск */
    return present;
}

static void setup(u32 lba, u8 cmd)
{
    outb(DRV, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(SECT, 1);
    outb(LBAL, (u8)lba);
    outb(LBAM, (u8)(lba >> 8));
    outb(LBAH, (u8)(lba >> 16));
    outb(CMD, cmd);
}

int ata_read(u32 lba, void *buf)
{
    if (!present) return -1;
    setup(lba, 0x20);
    if (wait_ready(1) < 0) return -1;
    u16 *p = buf;
    for (int i = 0; i < 256; i++) p[i] = inw(DATA);
    return 0;
}

int ata_write(u32 lba, const void *buf)
{
    if (!present) return -1;
    setup(lba, 0x30);
    if (wait_ready(1) < 0) return -1;
    const u16 *p = buf;
    for (int i = 0; i < 256; i++) outw(DATA, p[i]);
    outb(CMD, 0xE7);                          /* сбросить кэш */
    return wait_ready(0);
}
