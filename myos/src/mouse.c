#include "mouse.h"
#include "io.h"
#include "framebuffer.h"
#include "tty.h"
#include "serial.h"
#include "irq.h"
#include "pic.h"

#define PS2_DATA 0x60
#define PS2_STAT 0x64
#define PS2_CMD  0x64

static int mx = 512, my = 384;
static uint8_t btns = 0;
static int  cycle = 0;
static uint8_t packet[3];
static int  initialized = 0;
static int  verbose = 0;

/* Forward */
static void mouse_process_byte(uint8_t b);

/* --- PS/2 low level --- */
static void wait_write(void) {
    for (int t = 0; t < 100000; t++)
        if ((inb(PS2_STAT) & 0x02) == 0) return;
}
static int wait_read(void) {
    for (int t = 0; t < 100000; t++)
        if (inb(PS2_STAT) & 0x01) return 0;
    return -1;
}
static void ctrl_write(uint8_t v) { wait_write(); outb(PS2_CMD, v); }
static void data_write(uint8_t v) { wait_write(); outb(PS2_DATA, v); }
static uint8_t data_read(void) { if (wait_read() != 0) return 0xEE; return inb(PS2_DATA); }
static int mouse_cmd(uint8_t cmd) {
    ctrl_write(0xD4);
    data_write(cmd);
    return data_read() == 0xFA ? 0 : -1;
}

/* --- IRQ12 handler (правильный тип для irq.h) --- */
static void mouse_irq(struct regs* r) {
    (void)r;
    uint8_t st = inb(PS2_STAT);
    if (!(st & 0x01)) return;
    uint8_t b = inb(PS2_DATA);
    if (verbose) {
        serial_puts("[irq12] b="); serial_put_hex(b); serial_putc('\n');
    }
    mouse_process_byte(b);
}

/* --- Обработка одного байта --- */
static void mouse_process_byte(uint8_t b) {
    switch (cycle) {
        case 0:
            if (!(b & 0x08)) return;
            packet[0] = b; cycle = 1; break;
        case 1:
            packet[1] = b; cycle = 2; break;
        case 2: {
            packet[2] = b; cycle = 0;
            uint8_t fl = packet[0];
            int dx = packet[1];
            int dy = packet[2];
            if (fl & 0x10) dx -= 256;
            if (fl & 0x20) dy -= 256;
            btns = fl & 0x07;
            mx += dx;
            my -= dy;
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            if (mx >= (int)fb_width() - 8)  mx = (int)fb_width() - 9;
            if (my >= (int)fb_height() - 8) my = (int)fb_height() - 9;
            break;
        }
    }
}

/* --- Инициализация --- */
void mouse_init(void) {
    if (initialized) return;
    initialized = 1;

    for (int i = 0; i < 64; i++) {
        if ((inb(PS2_STAT) & 0x01) == 0) break;
        inb(PS2_DATA);
    }

    ctrl_write(0xA8);

    ctrl_write(0x20);
    uint8_t cfg = data_read();
    serial_puts("[mouse] cfg in: "); serial_put_hex(cfg); serial_putc('\n');

    cfg |=  0x02;
    cfg &= ~0x20;
    cfg &= ~0x10;

    ctrl_write(0x60);
    data_write(cfg);
    serial_puts("[mouse] cfg out: "); serial_put_hex(cfg); serial_putc('\n');

    int r1 = mouse_cmd(0xF6);
    int r2 = mouse_cmd(0xF4);
    serial_puts("[mouse] F6="); serial_put_hex(r1);
    serial_puts(" F4="); serial_put_hex(r2); serial_putc('\n');

    irq_install_handler(12, mouse_irq);
    pic_clear_mask(2);
    pic_clear_mask(12);

    serial_puts("[mouse] init done\n");
    tty_puts("[ok] PS/2 mouse initialized (IRQ12 + polling)\n");
}

/* --- Polling (вызывается из timer_cb) --- */
void mouse_poll(void) {
    if (!initialized) return;

    for (int iter = 0; iter < 16; iter++) {
        uint8_t st = inb(PS2_STAT);
        if (!(st & 0x01)) break;
        uint8_t b = inb(PS2_DATA);

        if (verbose) {
            serial_puts("[poll] st=");
            serial_put_hex(st);
            serial_puts(" b=");
            serial_put_hex(b);
            serial_putc('\n');
        }

        int is_mouse = (st & 0x20) ? 1 : 0;
        if (!is_mouse) {
            if (cycle == 0) {
                if ((b & 0x08) && !(b & 0xC0)) is_mouse = 1;
            } else {
                is_mouse = 1;
            }
        }
        if (!is_mouse) continue;
        mouse_process_byte(b);
    }
}

void mouse_toggle_verbose(void) { verbose = !verbose; }

int mouse_x(void)         { return mx; }
int mouse_y(void)         { return my; }
int mouse_btn_left(void)  { return btns & 1; }
int mouse_btn_right(void) { return btns & 2; }
int mouse_btn_middle(void){ return btns & 4; }
