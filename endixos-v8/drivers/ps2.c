/* PS/2: контроллер 8042, клавиатура (скан-коды набора 1) и мышь (3-байтовые пакеты). */
#include "ps2.h"
#include "pic.h"
#include "serial.h"
#include "../kernel/io.h"
#include "../kernel/idt.h"
#include "../kernel/event.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define ST_OUT_FULL 0x01
#define ST_IN_FULL  0x02
#define ST_MOUSE    0x20

/* ---------- низкоуровневый обмен с контроллером ---------- */

static int wait_write(void)
{
    for (u32 i = 0; i < 200000; i++)
        if (!(inb(PS2_STATUS) & ST_IN_FULL))
            return 0;
    return -1;
}

static int wait_read(void)
{
    for (u32 i = 0; i < 200000; i++)
        if (inb(PS2_STATUS) & ST_OUT_FULL)
            return 0;
    return -1;
}

static void ctrl_cmd(u8 c)
{
    wait_write();
    outb(PS2_CMD, c);
}

static void dev_write(u8 d)
{
    wait_write();
    outb(PS2_DATA, d);
}

static int dev_read(void)
{
    if (wait_read() != 0)
        return -1;
    return inb(PS2_DATA);
}

static void flush(void)
{
    for (int i = 0; i < 64 && (inb(PS2_STATUS) & ST_OUT_FULL); i++)
        inb(PS2_DATA);
}

static int mouse_cmd(u8 c)
{
    ctrl_cmd(0xD4);                       /* следующий байт - устройству 2 (мыши) */
    dev_write(c);
    return dev_read();                    /* ожидаем ACK = 0xFA */
}

/* ---------- мышь ---------- */

static i32 mx, my, bound_w = 1024, bound_h = 768;
static u8  pkt[3];
static int pkt_len;

void ps2_set_bounds(u32 width, u32 height)
{
    bound_w = (i32)width;
    bound_h = (i32)height;
    mx = bound_w / 2;
    my = bound_h / 2;
}

static void mouse_byte(u8 b)
{
    if (pkt_len == 0 && !(b & 0x08))
        return;                           /* потеряна синхронизация: первый байт всегда с битом 3 */
    pkt[pkt_len++] = b;
    if (pkt_len < 3)
        return;
    pkt_len = 0;

    u8 f = pkt[0];
    if (f & 0xC0)
        return;                           /* переполнение счётчиков: пакет отбрасываем */

    i32 dx = (i32)pkt[1] - (i32)((f << 4) & 0x100);
    i32 dy = (i32)pkt[2] - (i32)((f << 3) & 0x100);

    mx += dx;
    my -= dy;                             /* у мыши Y растёт вверх, у экрана - вниз */
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > bound_w - 1) mx = bound_w - 1;
    if (my > bound_h - 1) my = bound_h - 1;

    event_push(EV_MOUSE, mx, my, f & 7);
}

/* ---------- клавиатура ---------- */

/* Скан-коды набора 1, US-раскладка, индекс = скан-код (0x00..0x39). */
static const char map_normal[58] =
    "\0\033" "1234567890-=" "\b\t" "qwertyuiop[]" "\n" "\0" "asdfghjkl;'`" "\0"
    "\\" "zxcvbnm,./" "\0" "*" "\0" " ";
static const char map_shift[58] =
    "\0\033" "!@#$%^&*()_+" "\b\t" "QWERTYUIOP{}" "\n" "\0" "ASDFGHJKL:\"~" "\0"
    "|" "ZXCVBNM<>?" "\0" "*" "\0" " ";

static int k_shift, k_ctrl, k_caps, k_ext;

static void kbd_byte(u8 b)
{
    if (b == 0xE0 || b == 0xE1) {         /* расширенные клавиши (стрелки и т.п.) пока игнорируем */
        k_ext = 1;
        return;
    }
    int release = b & 0x80;
    u8 sc = b & 0x7F;

    if (k_ext) {
        k_ext = 0;
        if (sc == 0x1D) {                 /* правый Ctrl */
            k_ctrl = !release;
            return;
        }
        if (release)
            return;
        switch (sc) {                     /* стрелки и Delete */
        case 0x48: event_push(EV_KEY, KEY_UP,     k_ctrl, 0); break;
        case 0x50: event_push(EV_KEY, KEY_DOWN,   k_ctrl, 0); break;
        case 0x4B: event_push(EV_KEY, KEY_LEFT,   k_ctrl, 0); break;
        case 0x4D: event_push(EV_KEY, KEY_RIGHT,  k_ctrl, 0); break;
        case 0x53: event_push(EV_KEY, KEY_DELETE, k_ctrl, 0); break;
        default: break;
        }
        return;
    }

    if (sc == 0x2A || sc == 0x36) { k_shift = !release; return; }
    if (sc == 0x1D)               { k_ctrl  = !release; return; }
    if (release)
        return;
    if (sc == 0x3A) { k_caps = !k_caps; return; }
    if (sc == 0x3C) {                     /* F2 */
        event_push(EV_KEY, KEY_F2, k_ctrl, 0);
        return;
    }
    if (sc >= sizeof(map_normal))
        return;

    char c = map_normal[sc];
    if (c >= 'a' && c <= 'z') {
        if (k_shift ^ k_caps)
            c = (char)(c - 32);
    } else if (k_shift) {
        c = map_shift[sc];
    }
    if (c)
        event_push(EV_KEY, (u8)c, k_ctrl, 0);
}

/* ---------- обработчик IRQ1 и IRQ12 ---------- */

static void ps2_irq(void)
{
    /* Оба прерывания читают один порт, поэтому разбираем всё, что накопилось,
     * и по биту статуса решаем, чей это байт. */
    for (int i = 0; i < 64; i++) {
        u8 st = inb(PS2_STATUS);
        if (!(st & ST_OUT_FULL))
            break;
        u8 b = inb(PS2_DATA);
        if (st & ST_MOUSE)
            mouse_byte(b);
        else
            kbd_byte(b);
    }
}

void ps2_init(void)
{
    ctrl_cmd(0xAD);                       /* отключить клавиатурный порт */
    ctrl_cmd(0xA7);                       /* отключить мышиный порт */
    flush();

    ctrl_cmd(0x20);                       /* прочитать байт конфигурации */
    int cfg = dev_read();
    if (cfg < 0)
        cfg = 0;
    serial_puts("PS/2: config byte ");
    serial_put_hex((u32)cfg);
    serial_puts("\n");

    cfg |= 0x03;                          /* разрешить IRQ1 и IRQ12 */
    cfg |= 0x40;                          /* трансляция в набор 1 (на всякий случай) */
    cfg &= ~0x30;                         /* включить тактирование обоих портов */
    ctrl_cmd(0x60);
    dev_write((u8)cfg);

    ctrl_cmd(0xAE);                       /* включить клавиатурный порт */
    ctrl_cmd(0xA8);                       /* включить мышиный порт */

    dev_write(0xF4);                      /* клавиатура: включить сканирование */
    int ack_kbd = dev_read();

    int ack_def   = mouse_cmd(0xF6);      /* мышь: настройки по умолчанию */
    int ack_start = mouse_cmd(0xF4);      /* мышь: начать передачу данных */
    serial_puts("PS/2: ack kbd=");
    serial_put_hex((u32)ack_kbd);
    serial_puts(" mouse defaults=");
    serial_put_hex((u32)ack_def);
    serial_puts(" mouse enable=");
    serial_put_hex((u32)ack_start);
    serial_puts("\n");

    flush();
    pkt_len = 0;

    irq_register(1, ps2_irq);
    irq_register(12, ps2_irq);
    pic_unmask(1);
    pic_unmask(12);
}
