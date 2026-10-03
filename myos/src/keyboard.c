#include "keyboard.h"
#include "irq.h"
#include "io.h"

#define KBD_DATA 0x60
#define KBD_STAT 0x64

static const char kbd_map[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t','q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ',
};
static const char kbd_map_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t','Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' ',
};

static int buf[KEY_BUF_SIZE];
static volatile int head = 0;
static volatile int tail = 0;

static int shift = 0, caps = 0, ctrl = 0, ext = 0;

static void buf_push(int c) {
    int next = (head + 1) % KEY_BUF_SIZE;
    if (next != tail) { buf[head] = c; head = next; }
}

static void kbd_cb(struct regs* r) {
    (void)r;
    if (inb(KBD_STAT) & 0x20) return;
    uint8_t sc = inb(KBD_DATA);

    if (sc == 0xE0) { ext = 1; return; }

    if (sc & 0x80) {
        uint8_t rel = sc & 0x7F;
        if (rel == 0x2A || rel == 0x36) shift = 0;
        if (rel == 0x1D) ctrl = 0;
        ext = 0;
        return;
    }

    if (ext) {
        ext = 0;
        switch (sc) {
            case 0x48: buf_push(KEY_UP);     return;
            case 0x50: buf_push(KEY_DOWN);   return;
            case 0x4B: buf_push(KEY_LEFT);   return;
            case 0x4D: buf_push(KEY_RIGHT);  return;
            case 0x53: buf_push(KEY_DELETE); return;
            case 0x47: buf_push(KEY_HOME);   return;
            case 0x4F: buf_push(KEY_END);    return;
            case 0x57: buf_push(KEY_F11);    return;
            case 0x58: buf_push(KEY_F12);    return;
            default: return;
        }
    }

    /* F1-F10 */
    if (sc >= 0x3B && sc <= 0x44) {
        int fnum = sc - 0x3B + 1;
        buf_push(KEY_F1 + fnum - 1);
        return;
    }
    /* F11, F12 (extended, но у нас уже прошёл через ext выше) */

    if (sc == 0x2A || sc == 0x36) { shift = 1; return; }
    if (sc == 0x1D) { ctrl = 1; return; }
    if (sc == 0x3A) { caps = !caps; return; }
    if (sc > 127) return;

    char c = shift ? kbd_map_shift[sc] : kbd_map[sc];
    if (!c) return;
    if (caps && c >= 'a' && c <= 'z') c -= 32;
    else if (caps && shift && c >= 'A' && c <= 'Z') c += 32;
    if (ctrl) {
        if (c >= 'a' && c <= 'z') c = c - 'a' + 1;
        else if (c >= 'A' && c <= 'Z') c = c - 'A' + 1;
    }
    buf_push((int)(unsigned char)c);
}

void keyboard_init(void) {
    /* Сброс PS/2 контроллера — на случай, если QEMU оставил мусор */
    for (int i = 0; i < 64; i++) {
        if ((inb(KBD_STAT) & 0x01) == 0) break;
        inb(KBD_DATA);
    }

    /* Включить клавиатурный порт */
    while (inb(KBD_STAT) & 0x02) { }
    outb(KBD_STAT, 0xAE);

    /* Enable keyboard */
    while (inb(KBD_STAT) & 0x02) { }
    outb(KBD_DATA, 0xF4);

    irq_install_handler(1, kbd_cb);
}

int keyboard_has_char(void) { return head != tail; }

int keyboard_getchar(void) {
    while (head == tail) __asm__ volatile ("hlt");
    int c = buf[tail];
    tail = (tail + 1) % KEY_BUF_SIZE;
    return c;
}
