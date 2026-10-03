#include "e1000.h"
#include "pci.h"
#include "io.h"
#include "serial.h"
#include "tty.h"

#define E1000_VENDOR 0x8086

/* Registers */
#define REG_CTRL      0x0000
#define REG_STATUS    0x0008
#define REG_EERD      0x0014
#define REG_IMC       0x00D8
#define REG_RCTL      0x0100
#define REG_TCTL      0x0400
#define REG_TIPG      0x0410
#define REG_RDBAL     0x2800
#define REG_RDBAH     0x2804
#define REG_RDLEN     0x2808
#define REG_RDH       0x2810
#define REG_RDT       0x2818
#define REG_TDBAL     0x3800
#define REG_TDBAH     0x3804
#define REG_TDLEN     0x3808
#define REG_TDH       0x3810
#define REG_TDT       0x3818
#define REG_MTA       0x5200

#define CTRL_RST      (1u << 26)
#define CTRL_SLU      (1u << 6)

#define RCTL_EN       (1u << 1)
#define RCTL_BAM      (1u << 15)
#define RCTL_BSIZE_2048 (0u << 16)
#define RCTL_SECRC    (1u << 26)

#define TCTL_EN       (1u << 1)
#define TCTL_PSP      (1u << 3)
#define TCTL_CT_SHIFT 4
#define TCTL_COLD_SHIFT 12

struct tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

struct rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

#define TXD_CMD_EOP  (1u << 0)
#define TXD_CMD_IFCS (1u << 1)
#define TXD_CMD_RS   (1u << 3)
#define TXD_STAT_DD  (1u << 0)

#define RXD_STAT_DD  (1u << 0)

#define TX_RING_SIZE 8
#define RX_RING_SIZE 128
#define BUF_SIZE     2048

static volatile uint32_t* mmio = 0;
static int ok = 0;
static uint8_t mac[6];

static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buf[TX_RING_SIZE][BUF_SIZE] __attribute__((aligned(16)));
static int tx_cur = 0;

static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static uint8_t rx_buf[RX_RING_SIZE][BUF_SIZE] __attribute__((aligned(16)));
static int rx_cur = 0;

static inline uint32_t reg_read(uint32_t off) { return mmio[off / 4]; }
static inline void reg_write(uint32_t off, uint32_t val) { mmio[off / 4] = val; }

static uint16_t eeprom_read(uint8_t addr) {
    reg_write(REG_EERD, ((uint32_t)addr << 8) | 1);
    for (int i = 0; i < 100000; i++) {
        uint32_t v = reg_read(REG_EERD);
        if (v & (1u << 4)) return (v >> 16) & 0xFFFF;
        io_wait();
    }
    return 0xFFFF;
}

int e1000_init(void) {
    uint8_t bus, slot, func;
    if (pci_find(0x02, 0x00, 0x00, &bus, &slot, &func) != 0) {
        serial_puts("[e1000] no NIC\n");
        return -1;
    }

    uint32_t id = pci_read32(bus, slot, func, 0);
    uint16_t vendor = id & 0xFFFF;
    if (vendor != E1000_VENDOR) {
        serial_puts("[e1000] wrong vendor\n");
        return -1;
    }

    uint32_t bar0 = pci_read32(bus, slot, func, 0x10) & 0xFFFFFFF0;
    mmio = (volatile uint32_t*)bar0;
    serial_puts("[e1000] mmio="); serial_put_hex(bar0); serial_putc('\n');

    /* Enable memory space + bus master */
    uint32_t cmd = pci_read32(bus, slot, func, 0x04);
    pci_write32(bus, slot, func, 0x04, cmd | 0x06);

    /* Reset */
    reg_write(REG_CTRL, reg_read(REG_CTRL) | CTRL_RST);
    for (int i = 0; i < 1000000; i++) {
        if (!(reg_read(REG_CTRL) & CTRL_RST)) break;
        io_wait();
    }

    reg_write(REG_IMC, 0xFFFFFFFF);   /* mask all interrupts */

    /* MAC from EEPROM */
    for (int i = 0; i < 3; i++) {
        uint16_t w = eeprom_read(i);
        mac[i*2]     = w & 0xFF;
        mac[i*2 + 1] = (w >> 8) & 0xFF;
    }

    serial_puts("[e1000] mac=");
    for (int i = 0; i < 6; i++) {
        serial_put_hex(mac[i]);
        if (i < 5) serial_putc(':');
    }
    serial_putc('\n');

    /* Установить MAC в фильтр приёма — без этого NIC дропает пакеты */
    uint32_t ral = mac[0] | (mac[1] << 8) | (mac[2] << 16) | (mac[3] << 24);
    uint32_t rah = mac[4] | (mac[5] << 8) | (1u << 31);
    reg_write(0x5400, ral);
    reg_write(0x5404, rah);
    serial_puts("[e1000] RAL set\n");

    /* TX ring */
    for (int i = 0; i < TX_RING_SIZE; i++) {
        tx_ring[i].addr = (uint32_t)tx_buf[i];
        tx_ring[i].status = TXD_STAT_DD;
    }
    reg_write(REG_TDBAL, (uint32_t)&tx_ring[0]);
    reg_write(REG_TDBAH, 0);
    reg_write(REG_TDLEN, TX_RING_SIZE * sizeof(struct tx_desc));
    reg_write(REG_TDH, 0);
    reg_write(REG_TDT, 0);
    reg_write(REG_TCTL,
        TCTL_EN | TCTL_PSP | (15u << TCTL_CT_SHIFT) | (64u << TCTL_COLD_SHIFT));
    reg_write(REG_TIPG, 0x0060200A);

    /* RX ring */
    for (int i = 0; i < RX_RING_SIZE; i++) {
        rx_ring[i].addr = (uint32_t)rx_buf[i];
        rx_ring[i].status = 0;
    }
    reg_write(REG_RDBAL, (uint32_t)&rx_ring[0]);
    reg_write(REG_RDBAH, 0);
    reg_write(REG_RDLEN, RX_RING_SIZE * sizeof(struct rx_desc));
    reg_write(REG_RDH, 0);
    reg_write(REG_RDT, RX_RING_SIZE - 1);
    reg_write(REG_RCTL, RCTL_EN | RCTL_BAM | (1u<<3) | (1u<<4) | RCTL_BSIZE_2048 | RCTL_SECRC);

    /* Clear multicast table */
    for (int i = 0; i < 128; i++) reg_write(REG_MTA + i*4, 0);

    /* Force link up */
    reg_write(REG_CTRL, reg_read(REG_CTRL) | CTRL_SLU);

    ok = 1;

    /* === Диагностика === */
    serial_puts("[e1000] after init:\n");
    serial_puts("  CTRL   = "); serial_put_hex(reg_read(REG_CTRL));   serial_putc('\n');
    serial_puts("  STATUS = "); serial_put_hex(reg_read(REG_STATUS)); serial_putc('\n');
    serial_puts("  RCTL   = "); serial_put_hex(reg_read(REG_RCTL));   serial_putc('\n');
    serial_puts("  RDH    = "); serial_put_hex(reg_read(REG_RDH));    serial_putc('\n');
    serial_puts("  RDT    = "); serial_put_hex(reg_read(REG_RDT));    serial_putc('\n');
    serial_puts("  TCTL   = "); serial_put_hex(reg_read(REG_TCTL));   serial_putc('\n');

    uint32_t status = reg_read(REG_STATUS);
    serial_puts("  Link Up? ");
    serial_puts((status & 2) ? "YES" : "NO");
    serial_putc('\n');

    tty_puts("[ok] e1000 ready\n");
    return 0;
}

int e1000_ready(void) { return ok; }
void e1000_get_mac(uint8_t* out) { for (int i = 0; i < 6; i++) out[i] = mac[i]; }

int e1000_send(const void* data, uint32_t len) {
    if (!ok || len > BUF_SIZE) return -1;

    for (int i = 0; i < 1000000; i++) {
        if (tx_ring[tx_cur].status & TXD_STAT_DD) break;
        io_wait();
    }

    const uint8_t* src = (const uint8_t*)data;
    for (uint32_t i = 0; i < len; i++) tx_buf[tx_cur][i] = src[i];

    tx_ring[tx_cur].length = len;
    tx_ring[tx_cur].cmd = TXD_CMD_EOP | TXD_CMD_IFCS | TXD_CMD_RS;
    tx_ring[tx_cur].status = 0;

    tx_cur = (tx_cur + 1) % TX_RING_SIZE;
    reg_write(REG_TDT, tx_cur);
    return 0;
}

int e1000_recv(void* buf, uint32_t max) {
    if (!ok) return 0;
    uint8_t* d = (uint8_t*)buf;

    struct rx_desc* rd = &rx_ring[rx_cur];
    if (!(rd->status & RXD_STAT_DD)) return 0;

    uint32_t len = rd->length;
    if (len > max) len = max;
    for (uint32_t i = 0; i < len; i++) d[i] = rx_buf[rx_cur][i];

    rd->status = 0;

    /* Сдвиг RDT ВСЕГДА на (rx_cur + 1) и обновление rx_cur */
    rx_cur = (rx_cur + 1) % RX_RING_SIZE;
    /* Ставим RDT на следующий после обработанного */
    reg_write(REG_RDT, rx_cur);

    return len;
}

