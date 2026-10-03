#include "rtl8139.h"
#include "pci.h"
#include "io.h"
#include "serial.h"
#include "tty.h"

#define RTL_VENDOR 0x10EC
#define RTL_DEVICE 0x8139

/* Registers */
#define RTL_IDR0     0x00
#define RTL_TSD0     0x10
#define RTL_TSAD0    0x20
#define RTL_RBSTART  0x30
#define RTL_CMD      0x37
#define RTL_CAPR     0x38
#define RTL_CBR      0x3A
#define RTL_IMR      0x3C
#define RTL_ISR      0x3E
#define RTL_TCR      0x40
#define RTL_RCR      0x44

static uint16_t rtl_io = 0;
static uint8_t  mac[6];
static int      ok = 0;

/* Буферы */
static uint8_t tx_buffer[4][2048] __attribute__((aligned(16)));
/* always use buffer 0 */
static uint8_t rx_buffer[8192 + 16 + 1536] __attribute__((aligned(16)));
static int      tx_cur = 0;
static uint32_t rx_cur = 0;

static inline uint8_t  r8 (uint16_t r) { return inb(rtl_io + r); }
static inline uint16_t r16(uint16_t r) { return inw(rtl_io + r); }
static inline uint32_t r32(uint16_t r) { return inl(rtl_io + r); }
static inline void     w8 (uint16_t r, uint8_t v)  { outb(rtl_io + r, v); }
static inline void     w16(uint16_t r, uint16_t v) { outw(rtl_io + r, v); }
static inline void     w32(uint16_t r, uint32_t v) { outl(rtl_io + r, v); }

int rtl8139_init(void) {
    uint8_t bus, slot, func;
    if (pci_find(0x02, 0x00, 0x00, &bus, &slot, &func) != 0) {
        serial_puts("[rtl] no NIC found\n");
        return -1;
    }

    uint32_t bar0 = pci_read32(bus, slot, func, 0x10);
    rtl_io = bar0 & 0xFFFC;

    serial_puts("[rtl] io="); serial_put_hex(rtl_io); serial_putc('\n');

    /* Bus mastering + I/O space */
    uint32_t cmd = pci_read32(bus, slot, func, 0x04);
    cmd |= 0x05;
    pci_write32(bus, slot, func, 0x04, cmd);

    /* Software reset */
    w8(RTL_CMD, 0x10);
    int t = 0;
    while ((r8(RTL_CMD) & 0x10) && t < 100000) { io_wait(); t++; }

    /* MAC */
    for (int i = 0; i < 6; i++) mac[i] = r8(RTL_IDR0 + i);

    serial_puts("[rtl] mac=");
    for (int i = 0; i < 6; i++) {
        serial_put_hex(mac[i]);
        if (i < 5) serial_putc(':');
    }
    serial_putc('\n');

    /* RX buffer */
    w32(RTL_RBSTART, (uint32_t)rx_buffer);

    /* Разрешить RX и TX */
    w8(RTL_CMD, 0x0C);   /* RE=1, TE=1 */

    /* Все прерывания */
    w16(RTL_IMR, 0x0005);

    /* RCR: принять всё (broadcast + multicast + phys + all) */
    w32(RTL_RCR, 0x0000000F | 0x00000080);

    /* TCR */
    w32(RTL_TCR, 0x03000000);

    /* Очистить ISR */
    w16(RTL_ISR, 0xFFFF);

    ok = 1;
    tty_puts("[ok] RTL8139 ready\n");
    return 0;
}

void rtl8139_get_mac(uint8_t* out) {
    for (int i = 0; i < 6; i++) out[i] = mac[i];
}

int rtl8139_ready(void) { return ok; }

int rtl8139_send(const void* data, uint32_t len) {
    if (!ok || len > 2048) return -1;

    /* Один буфер, простая пауза. Без TOK-чеков. */
    const uint8_t* src = (const uint8_t*)data;
    for (uint32_t i = 0; i < len; i++) tx_buffer[0][i] = src[i];

    w32(RTL_TSAD0, (uint32_t)tx_buffer[0]);
    w32(RTL_TSD0, len & 0x1FFF);

    /* Ждём ~1 мс чтобы карта обработала */
    for (volatile int i = 0; i < 10000; i++);

    return 0;
}





int rtl8139_recv(void* buf, uint32_t max) {
    if (!ok) return 0;

    uint16_t cbr = r16(RTL_CBR);
    if (cbr == (uint16_t)rx_cur) return 0;

    uint8_t* rb = rx_buffer;
    uint16_t status = *(uint16_t*)(rb + rx_cur);
    uint16_t length = *(uint16_t*)(rb + rx_cur + 2);

    if (!(status & 0x01)) {
        /* Не OK — пропустить */
        rx_cur = (rx_cur + length + 4 + 3) & ~3u;
        if (rx_cur >= 8192) rx_cur -= 8192;
        w16(RTL_CAPR, (rx_cur - 16) & 0xFFFF);
        return 0;
    }
    if (length < 4 || length > 2048) return 0;

    uint32_t data_len = length - 4;
    if (data_len > max) data_len = max;

    for (uint32_t i = 0; i < data_len; i++)
        ((uint8_t*)buf)[i] = rb[rx_cur + 4 + i];

    rx_cur = (rx_cur + length + 4 + 3) & ~3u;
    if (rx_cur >= 8192) rx_cur -= 8192;
    w16(RTL_CAPR, (rx_cur - 16) & 0xFFFF);

    return data_len;
}
