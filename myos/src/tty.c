#include "tty.h"
#include "framebuffer.h"

static const uint32_t ega_to_rgb[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

#define GLYPH_W     8
#define GLYPH_H     8
#define SCALE       2
#define CELL_W      (GLYPH_W*SCALE)
#define CELL_H      (GLYPH_H*SCALE)

static int      cols = 0, rows = 0;
static int      col  = 0, row  = 0;
static uint8_t  color = 10;

extern void gfx_flush(void);

void tty_set_color(uint8_t c) { color = c & 0x0F; }

void tty_clear(void) {
    cols = fb_width()  / CELL_W;
    rows = fb_height() / CELL_H;
    col = row = 0;
    fb_fill(ega_to_rgb[0]);
    gfx_flush();
}

static void scroll_up(void) {
    extern void fb_scroll_up(int);
    fb_scroll_up(CELL_H);
}

void tty_flush(void) { gfx_flush(); }

void tty_putc(char c) {
    if (c == '\r') { col = 0; return; }
    if (c == '\b') {
        if (col > 0) col--;
        else if (row > 0) { row--; col = cols - 1; }
        return;
    }
    if (c == '\n') {
        col = 0; row++;
        if (row >= rows) { scroll_up(); row = rows - 1; }
        return;
    }
    uint32_t px = col * CELL_W;
    uint32_t py = row * CELL_H;
    fb_putc(c, px, py, ega_to_rgb[color], SCALE);
    if (++col >= cols) {
        col = 0; row++;
        if (row >= rows) { scroll_up(); row = rows - 1; }
    }
}

void tty_puts_nf(const char* s) {
    while (*s) tty_putc(*s++);
}

void tty_puts(const char* s) {
    tty_puts_nf(s);
    tty_flush();
}

void tty_put_hex(uint32_t v) {
    tty_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t n = (v >> i) & 0xF;
        tty_putc(n < 10 ? '0' + n : 'a' + n - 10);
    }
}

/* Залить текущую клетку цветом фона (стереть символ под курсором) */
void tty_erase_cell(void) {
    uint32_t px = col * CELL_W;
    uint32_t py = row * CELL_H;
    extern void fb_rect(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    fb_rect(px, py, CELL_W, CELL_H, ega_to_rgb[0]);
}

/* Настоящее стирание символа (сдвиг курсора + затирание клетки) */
void tty_backspace(void) {
    if (col == 0 && row == 0) return;
    if (col == 0) { row--; col = cols - 1; }
    else col--;
    uint32_t px = col * CELL_W;
    uint32_t py = row * CELL_H;
    fb_rect(px, py, CELL_W, CELL_H, ega_to_rgb[0]);
    tty_flush();
}
