#include "ata.h"
#include "io.h"
#include "tty.h"

#define PRIMARY_IO   0x1F0
#define PRIMARY_CTRL 0x3F6

#define REG_DATA      0
#define REG_ERROR     1
#define REG_FEATURES  1
#define REG_SECCOUNT  2
#define REG_LBA0      3
#define REG_LBA1      4
#define REG_LBA2      5
#define REG_HDDEVSEL  6
#define REG_CMD       7
#define REG_STATUS    7

#define CMD_READ_PIO  0x20
#define CMD_WRITE_PIO 0x30
#define CMD_IDENTIFY  0xEC

#define SR_BSY  0x80
#define SR_DRDY 0x40
#define SR_DF   0x20
#define SR_DRQ  0x08
#define SR_ERR  0x01

static int ata_ok = 0;

static uint8_t st(void)          { return inb(PRIMARY_IO + REG_STATUS); }
static void   wait_bsy(void)     { while (st() & SR_BSY) { } }
static void   wait_drq(void)     { while (!(st() & SR_DRQ)) { } }

int ata_init(void) {
    outb(PRIMARY_IO + REG_HDDEVSEL, 0xA0);
    for (int i = 0; i < 4; i++) inb(PRIMARY_CTRL);

    outb(PRIMARY_CTRL, 0x04);
    for (int i = 0; i < 4; i++) inb(PRIMARY_CTRL);
    outb(PRIMARY_CTRL, 0x00);
    wait_bsy();

    outb(PRIMARY_IO + REG_SECCOUNT, 0);
    outb(PRIMARY_IO + REG_LBA0, 0);
    outb(PRIMARY_IO + REG_LBA1, 0);
    outb(PRIMARY_IO + REG_LBA2, 0);
    outb(PRIMARY_IO + REG_CMD, CMD_IDENTIFY);

    uint8_t s = st();
    if (s == 0) { tty_puts("[ata] no drive on primary master\n"); return -1; }
    wait_bsy();

    if (inb(PRIMARY_IO + REG_LBA1) != 0 || inb(PRIMARY_IO + REG_LBA2) != 0) {
        tty_puts("[ata] not ATA\n");
        return -1;
    }

    while (1) {
        s = st();
        if (s & SR_ERR) { tty_puts("[ata] identify error\n"); return -1; }
        if (s & SR_DRQ) break;
    }

    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(PRIMARY_IO + REG_DATA);

    /* sectors count в словах 60-61 (LBA28) */
    uint32_t sectors = id[60] | ((uint32_t)id[61] << 16);

    tty_puts("[ok] ATA PIO drive: ");
    tty_put_hex(sectors);
    tty_puts(" sectors (");
    tty_put_hex(sectors * 512 / (1024*1024));
    tty_puts(" MB)\n");

    ata_ok = 1;
    return 0;
}

int ata_read_sector(uint32_t lba, uint8_t* buffer) {
    if (!ata_ok) return -1;
    wait_bsy();

    outb(PRIMARY_IO + REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(PRIMARY_IO + REG_FEATURES, 0);
    outb(PRIMARY_IO + REG_SECCOUNT, 1);
    outb(PRIMARY_IO + REG_LBA0, lba & 0xFF);
    outb(PRIMARY_IO + REG_LBA1, (lba >> 8) & 0xFF);
    outb(PRIMARY_IO + REG_LBA2, (lba >> 16) & 0xFF);
    outb(PRIMARY_IO + REG_CMD, CMD_READ_PIO);

    wait_bsy();
    wait_drq();

    uint16_t* buf = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) buf[i] = inw(PRIMARY_IO + REG_DATA);
    return 0;
}

int ata_write_sector(uint32_t lba, const uint8_t* buffer) {
    if (!ata_ok) return -1;
    wait_bsy();

    outb(PRIMARY_IO + REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(PRIMARY_IO + REG_FEATURES, 0);
    outb(PRIMARY_IO + REG_SECCOUNT, 1);
    outb(PRIMARY_IO + REG_LBA0, lba & 0xFF);
    outb(PRIMARY_IO + REG_LBA1, (lba >> 8) & 0xFF);
    outb(PRIMARY_IO + REG_LBA2, (lba >> 16) & 0xFF);
    outb(PRIMARY_IO + REG_CMD, CMD_WRITE_PIO);

    wait_bsy();
    wait_drq();

    const uint16_t* buf = (const uint16_t*)buffer;
    for (int i = 0; i < 256; i++) outw(PRIMARY_IO + REG_DATA, buf[i]);

    /* flush */
    outb(PRIMARY_IO + REG_CMD, 0xE7);
    wait_bsy();
    return 0;
}
