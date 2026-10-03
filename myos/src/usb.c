#include "usb.h"
#include "pci.h"
#include "io.h"
#include "tty.h"
#include "serial.h"
#include "framebuffer.h"

/* ===================== PCI / UHCI ===================== */

static uint16_t uhci_io = 0;
static int      uhci_ok = 0;

#define UHCI_CMD    0x00
#define UHCI_STS    0x02
#define UHCI_INTR   0x04
#define UHCI_FRNUM  0x06
#define UHCI_FLBASE 0x08
#define UHCI_SOF    0x0C
#define UHCI_PORT1  0x10
#define UHCI_PORT2  0x12

static inline uint16_t uhci_r16(uint16_t r) { return inw(uhci_io + r); }
static inline void     uhci_w16(uint16_t r, uint16_t v) { outw(uhci_io + r, v); }
static inline uint32_t uhci_r32(uint16_t r) { return inl(uhci_io + r); }
static inline void     uhci_w32(uint16_t r, uint32_t v) { outl(uhci_io + r, v); }

/* ===================== Общие структуры ===================== */

#define LINK_TERM  1u
#define LINK_QH    2u
#define LINK_VF    4u

struct qh {
    uint32_t head_link;
    uint32_t element_link;
    uint32_t reserved[2];
    uint32_t sw[4];
} __attribute__((packed, aligned(16)));

struct td {
    uint32_t link;
    uint32_t status;
    uint32_t token;
    uint32_t buffer;
    uint32_t reserved[4];
} __attribute__((packed, aligned(16)));

static uint32_t frame_list[1024] __attribute__((aligned(4096)));

/* Пул QH/TD — используем статические (без malloc, чтобы не мудрить) */
static struct qh qh_pool[16] __attribute__((aligned(16)));
static struct td td_pool[16] __attribute__((aligned(16)));

static uint8_t setup_packet[8] __attribute__((aligned(16)));
static uint8_t ctrl_buffer[256] __attribute__((aligned(16)));

/* ===================== UHCI init ===================== */

static int uhci_init(void) {
    uint8_t bus, slot, func;
    /* class 0x0C (serial bus), subclass 0x03 (USB), progif 0x00 (UHCI) */
    if (pci_find(0x0C, 0x03, 0x00, &bus, &slot, &func) != 0) {
        serial_puts("[usb] no UHCI controller\n");
        return -1;
    }

    /* BAR4 — I/O base */
    uint32_t bar4 = pci_read32(bus, slot, func, 0x20);
    uhci_io = bar4 & 0xFFFC;
    serial_puts("[usb] UHCI io="); serial_put_hex(uhci_io); serial_putc('\n');

    /* Включить bus mastering и I/O space */
    uint32_t cmd = pci_read32(bus, slot, func, 0x04);
    cmd |= 0x05;
    pci_write32(bus, slot, func, 0x04, cmd);

    /* Global reset */
    uhci_w16(UHCI_CMD, 0x0004);
    for (int i = 0; i < 100; i++) io_wait();
    uhci_w16(UHCI_CMD, 0x0000);
    for (int i = 0; i < 100; i++) io_wait();

    /* Остановить контроллер на время настройки */
    uhci_w16(UHCI_CMD, 0x0000);
    uhci_w16(UHCI_INTR, 0);

    /* Frame list — все терминаторы */
    for (int i = 0; i < 1024; i++) frame_list[i] = LINK_TERM;

    /* FLBASE — физический адрес frame list (у нас identity map = virtual) */
    uhci_w32(UHCI_FLBASE, (uint32_t)frame_list);
    uhci_w16(UHCI_FRNUM, 0);

    /* USBSTS — очистить все pending биты */
    uhci_w16(UHCI_STS, 0x3F);

    /* Порядок: сначала RS, потом CF. По спеке нужно CF=1 перед запуском,
       но некоторые HC QEMU ждут именно эту последовательность. */
    uhci_w16(UHCI_CMD, 0x0000);   /* всё выключено */
    for (int i = 0; i < 100; i++) io_wait();

    uhci_w16(UHCI_CMD, 0x0001);   /* RS=1, CF=0 */
    for (int i = 0; i < 1000; i++) io_wait();

    uhci_w16(UHCI_CMD, 0x0041);   /* RS=1, CF=1 */
    for (int i = 0; i < 1000; i++) io_wait();

    /* Переустановить FLBASE после CF — некоторые HC требуют этого */
    uhci_w32(UHCI_FLBASE, (uint32_t)frame_list);
    for (int i = 0; i < 1000; i++) io_wait();
    serial_puts("[usb] FLBASE reread="); serial_put_hex(uhci_r32(UHCI_FLBASE));
    serial_putc('\n');

    uint16_t cmd_r = uhci_r16(UHCI_CMD);
    uint16_t sts_r = uhci_r16(UHCI_STS);
    serial_puts("[usb] CMD="); serial_put_hex(cmd_r);
    serial_puts(" STS="); serial_put_hex(sts_r); serial_putc('\n');

    /* STS bit 5 (HCHalted) должен быть 0 */
    if (sts_r & 0x0020) {
        serial_puts("[usb] host controller halted after start\n");
        return -1;
    }

    /* ===== Включить порт 1 по спеке UHCI ===== */

    /* 1. Установить Port Reset (PR, bit 9) */
    uhci_w16(UHCI_PORT1, 0x0200);
    for (int i = 0; i < 20000; i++) io_wait();   /* >= 10 мс */

    /* 2. Снять Port Reset (PR=0) */
    uhci_w16(UHCI_PORT1, 0x0000);
    for (int i = 0; i < 20000; i++) io_wait();

    /* 3. Установить Port Enable (PE, bit 2).
       Не записываем CSC явно — оставляем её «1» чтобы HC сам сбросил
       change биты, а потом PE становится = 1. */
    uhci_w16(UHCI_PORT1, 0x0004);   /* PE=1, остальные write-1-to-clear = 0 */
    for (int i = 0; i < 20000; i++) io_wait();

    /* 4. Прочитать статус */
    uint16_t port = uhci_r16(UHCI_PORT1);
    serial_puts("[usb] port1 status = "); serial_put_hex(port); serial_putc('\n');

    if (!(port & 0x0001)) {
        serial_puts("[usb] no device on port1\n");
        return -1;
    }
    if (!(port & 0x0004)) {
        serial_puts("[usb] port not enabled after reset\n");
        return -1;
    }
    /* Сбросить все "change" биты записью обратно считанного значения */
    port = uhci_r16(UHCI_PORT1);
    uhci_w16(UHCI_PORT1, port);
    for (int i = 0; i < 1000; i++) io_wait();
    port = uhci_r16(UHCI_PORT1);
    serial_puts("[usb] port1 after clear = "); serial_put_hex(port); serial_putc('\n');

    serial_puts("[usb] port1 enabled OK\n");

    uhci_ok = 1;
    return 0;
}

/* ===================== Transfer ===================== */

/* PID коды */
#define PID_IN     0x69
#define PID_OUT    0xE1
#define PID_SETUP  0x2D

static void td_init(struct td* td, uint32_t pid, uint32_t addr, uint32_t ep,
                    uint32_t toggle, uint8_t* buf, uint32_t len, uint32_t next) {
    td->link   = next;
    td->status = 1u << 23;   /* Active bit — 23, не 24! */
    /* MaxLen в Token зависит от pid. Для SETUP — 7 (8 байт). Для IN/OUT — len */
    uint32_t maxlen;
    if (pid == PID_SETUP) maxlen = 7;
    else if (len == 0)    maxlen = 0x7FF;
    else                  maxlen = len - 1;
    td->buffer = (uint32_t)buf;
    for (int i = 0; i < 4; i++) td->reserved[i] = 0;
}

static void qh_init(struct qh* qh, uint32_t head_link, uint32_t element_link, uint32_t max_packet) {
    qh->head_link    = head_link;
    qh->element_link = element_link;
    /* hw_sw[0]: Max Packet Size (bits 16-26) + Control Endpoint Flag (bit 27) */
    qh->reserved[0]  = ((max_packet & 0x7FF) << 16) | (1u << 27);
    qh->reserved[1]  = 0;
    for (int i = 0; i < 4; i++) qh->sw[i] = 0;
}

/* Ждать завершения TD: возвращает 0 при успехе, -1 при ошибке */
static int td_wait(struct td* td) {
    /* Ждём пока Active (bit 23) сбросится */
    for (int i = 0; i < 50000000; i++) {
        if (!(td->status & (1u << 23))) break;
        io_wait();
    }
    if (td->status & (1u << 23)) {
        serial_puts("[td] TIMEOUT st="); serial_put_hex(td->status); serial_putc('\n');
        return -2;
    }

    uint32_t st = td->status;
    /* Настоящие ошибки: bit 17 (bitstuff), 18 (CRC), 20 (Babble),
       21 (Buffer), 22 (Stalled). Бит 19 (NAK) — норма, не ошибка. */
    if (st & 0x007A0000) {
        serial_puts("[td] ERR st="); serial_put_hex(st);
        serial_puts(" tok="); serial_put_hex(td->token); serial_putc('\n');
        return -1;
    }
    return 0;
}

/* Одна транзакция (single TD на frame). frame_entry — индекс в frame_list. */
static int uhci_run_one(struct td* td, int frame_slot) {
    (void)frame_slot;
    static struct qh qh __attribute__((aligned(16)));

    /* Debug: проверка выравнивания */
    if ((uint32_t)&qh & 0xF) serial_puts("[usb] QH NOT ALIGNED!\n");
    if ((uint32_t)td & 0xF)  serial_puts("[usb] TD NOT ALIGNED!\n");
    serial_puts("[u] qh="); serial_put_hex((uint32_t)&qh);
    serial_puts(" td="); serial_put_hex((uint32_t)td); serial_putc('\n');

    td->link = LINK_TERM;
    qh_init(&qh, LINK_TERM, (uint32_t)td, 8);

    /* Один слот, 3 кадра впереди текущего */
    uint16_t start = uhci_r16(UHCI_FRNUM) & 0x3FF;
    uint16_t slot  = (start + 3) & 0x3FF;
    frame_list[slot] = (uint32_t)&qh | LINK_QH;

    /* Ждём 6 кадров FRNUM */
    int ticks = 0;
    for (int i = 0; i < 30000000 && ticks < 6; i++) {
        uint16_t now = uhci_r16(UHCI_FRNUM) & 0x3FF;
        if (now != start) { ticks++; start = now; }
        io_wait();
    }

    /* ВАЖНО: убрать QH сразу после обработки.
       HC мог обработать TD на 3-м кадре, но за оставшиеся 3 кадра
       он попробует выполнить его снова — TD уже не Active, но QH
       всё ещё в списке. Это ломает контроллер. */
    frame_list[slot] = LINK_TERM;

    /* Теперь проверяем статус TD */
    return td_wait(td);
}





/* Контрольная передача: SETUP → IN/OUT (опционально) → STATUS */
static int usb_control(uint32_t dev_addr, uint8_t* setup, uint8_t* data, uint32_t data_len, int data_in) {
    static struct td tds[3] __attribute__((aligned(16)));
    static int frame = 1;

    /* --- SETUP --- */
    for (int i = 0; i < 8; i++) setup_packet[i] = setup[i];
    td_init(&tds[0], PID_SETUP, dev_addr, 0, 0, setup_packet, 8, LINK_TERM);
    {
        int r = uhci_run_one(&tds[0], frame++);
        serial_puts("[ctl] SETUP r="); serial_put_hex(r);
        serial_puts(" st="); serial_put_hex(tds[0].status); serial_putc('\n');
        if (r != 0) return -1;
    }

    /* --- DATA (если есть) --- */
    if (data_len > 0) {
        td_init(&tds[1], data_in ? PID_IN : PID_OUT, dev_addr, 0, 1,
                data, data_len, LINK_TERM);
        if (uhci_run_one(&tds[1], frame++) != 0) return -2;
    }

    /* --- STATUS (IN если data был OUT, OUT если data был IN или нет) --- */
    int status_pid = (data_len && data_in) ? PID_OUT : PID_IN;
    uint8_t dummy[8];
    td_init(&tds[2], status_pid, dev_addr, 0, 1, dummy, 0, LINK_TERM);
    if (uhci_run_one(&tds[2], frame++) != 0) return -3;

    if (frame > 1000) frame = 1;
    return 0;
}

/* ===================== USB enumeration ===================== */

static uint32_t tablet_addr = 0;
static uint8_t  tablet_ep_in = 0;
static uint8_t  tablet_max_packet = 8;

struct usb_device_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed));

static int usb_get_descriptor(uint32_t addr, uint8_t type, uint8_t index, uint8_t* buf, uint16_t len) {
    uint8_t setup[8];
    setup[0] = 0x80;   /* IN, standard, device */
    setup[1] = 0x06;   /* GET_DESCRIPTOR */
    setup[2] = index;
    setup[3] = type;
    setup[4] = 0; setup[5] = 0;
    setup[6] = len & 0xFF; setup[7] = (len >> 8) & 0xFF;
    return usb_control(addr, setup, buf, len, 1);
}

static int usb_set_address(uint32_t new_addr) {
    uint8_t setup[8];
    setup[0] = 0x00;
    setup[1] = 0x05;
    setup[2] = new_addr;
    setup[3] = 0;
    setup[4] = 0; setup[5] = 0;
    setup[6] = 0; setup[7] = 0;
    return usb_control(0, setup, 0, 0, 0);
}

static int usb_set_config(uint32_t addr, uint8_t cfg) {
    uint8_t setup[8];
    setup[0] = 0x00;
    setup[1] = 0x09;
    setup[2] = cfg;
    setup[3] = 0;
    setup[4] = 0; setup[5] = 0;
    setup[6] = 0; setup[7] = 0;
    return usb_control(addr, setup, 0, 0, 0);
}

int usb_tablet_init(void) {
    if (uhci_init() != 0) return -1;

    /* 1. Первое чтение device descriptor (только 8 байт, чтобы узнать max packet) */
    struct usb_device_desc dd;
    if (usb_get_descriptor(0, 0x01, 0, (uint8_t*)&dd, 8) != 0) {
        serial_puts("[usb] get desc fail\n");
        return -1;
    }
    serial_puts("[usb] maxpkt0="); serial_put_hex(dd.bMaxPacketSize0); serial_putc('\n');

    /* 2. Set address = 1 */
    if (usb_set_address(1) != 0) return -1;
    tablet_addr = 1;

    /* 3. Прочитать полный device descriptor */
    if (usb_get_descriptor(tablet_addr, 0x01, 0, (uint8_t*)&dd, 18) != 0) return -1;
    serial_puts("[usb] vid="); serial_put_hex(dd.idVendor);
    serial_puts(" pid="); serial_put_hex(dd.idProduct); serial_putc('\n');

    /* 4. Прочитать config descriptor (первые 9 байт) */
    uint8_t cfg_head[9];
    if (usb_get_descriptor(tablet_addr, 0x02, 0, cfg_head, 9) != 0) return -1;
    uint16_t cfg_total = cfg_head[2] | ((uint16_t)cfg_head[3] << 8);
    serial_puts("[usb] cfg total="); serial_put_hex(cfg_total); serial_putc('\n');
    if (cfg_total > 256) cfg_total = 256;

    /* 5. Полная config */
    if (usb_get_descriptor(tablet_addr, 0x02, 0, ctrl_buffer, cfg_total) != 0) return -1;

    /* 6. Найти endpoint IN (0x05) */
    tablet_ep_in = 0;
    for (uint32_t off = 0; off < cfg_total; ) {
        uint8_t blen = ctrl_buffer[off];
        uint8_t btype = ctrl_buffer[off + 1];
        if (blen == 0) break;
        if (btype == 0x05) {  /* endpoint */
            uint8_t ep_addr   = ctrl_buffer[off + 2];
            uint8_t ep_attr   = ctrl_buffer[off + 3];
            uint16_t mps      = ctrl_buffer[off + 4] | ((uint16_t)ctrl_buffer[off+5] << 8);
            if ((ep_addr & 0x80) && (ep_attr & 0x03) == 0x03) {  /* IN + Interrupt */
                tablet_ep_in = ep_addr & 0x0F;
                tablet_max_packet = mps & 0x7FF;
                serial_puts("[usb] ep_in="); serial_put_hex(tablet_ep_in);
                serial_puts(" mps="); serial_put_hex(tablet_max_packet);
                serial_putc('\n');
            }
        }
        off += blen;
    }

    if (tablet_ep_in == 0) {
        serial_puts("[usb] no interrupt IN endpoint\n");
        return -1;
    }

    /* 7. Set configuration 1 */
    if (usb_set_config(tablet_addr, 1) != 0) return -1;

    serial_puts("[usb] tablet ready\n");
    tty_puts("[ok] USB tablet ready\n");
    return 0;
}

/* ===================== Polling отчётов ===================== */

static int      tb_x = 512, tb_y = 384;
static uint8_t  tb_btns = 0;
static uint8_t  report_buf[8] __attribute__((aligned(16)));
static int      report_ready = 0;

/* Одиночная interrupt IN транзакция на endpoint tablet_ep_in */
static int usb_tablet_get_report(void) {
    static struct td td __attribute__((aligned(16)));
    static int frame = 500;

    uint8_t* buf = report_buf;
    uint32_t len = 6;   /* usb-tablet: 6-байтовый report protocol */

    td_init(&td, PID_IN, tablet_addr, tablet_ep_in, 0, buf, len, LINK_TERM);
    int rc = uhci_run_one(&td, frame++);
    if (frame > 1000) frame = 500;

    if (rc == 0) {
        uint32_t actual = (td.status >> 16) & 0x7FF;
        if (actual >= 4) {
            /* usb-tablet report format: [buttons][x_lo][x_hi][y_lo][y_hi][wheel] */
            tb_btns = buf[0];
            uint32_t x = buf[1] | ((uint32_t)buf[2] << 8);
            uint32_t y = buf[3] | ((uint32_t)buf[4] << 8);
            tb_x = (int)((x * fb_width())  / 0x7FFF);
            tb_y = (int)((y * fb_height()) / 0x7FFF);
            report_ready = 1;
        }
    }
    return rc;
}

/* Вызывать из timer_cb. Не блокирует — TD один раз за тик. */
void usb_tablet_poll(void) {
    if (!uhci_ok || tablet_addr == 0) return;
    usb_tablet_get_report();
}

int usb_tablet_ready(void)  { return report_ready; }
int usb_tablet_x(void)      { return tb_x; }
int usb_tablet_y(void)      { return tb_y; }
int usb_tablet_left(void)   { return tb_btns & 1; }
int usb_tablet_right(void)  { return tb_btns & 2; }
int usb_tablet_middle(void) { return tb_btns & 4; }
