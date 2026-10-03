#include "framebuffer.h"
#include "font8x8.h"
#include "tty.h"
#include "serial.h"

static uint8_t* fb_addr         = 0;
static uint32_t fb_w            = 0;
static uint32_t fb_h            = 0;
static uint32_t fb_pitch_bytes  = 0;
static uint8_t  fb_bpp          = 0;
static int      fb_ok           = 0;

/* Back buffer: BGRA-пиксели (как у нас рисуется) */
static uint32_t* backbuf = 0;

int fb_init(struct multiboot_info* mbi) {
    serial_puts("[fb] flags = "); serial_put_hex(mbi->flags); serial_putc('\n');

    if (mbi->flags & (1u << 12)) {
        uint64_t addr64 = mbi->framebuffer_addr;
        fb_addr        = (uint8_t*)(uint32_t)addr64;
        fb_w           = mbi->framebuffer_width;
        fb_h           = mbi->framebuffer_height;
        fb_pitch_bytes = mbi->framebuffer_pitch;
        fb_bpp         = mbi->framebuffer_bpp;
        if (fb_addr && fb_w && fb_h && fb_bpp) fb_ok = 1;
    } else if (mbi->flags & (1u << 11)) {
        uint8_t* mi = (uint8_t*)mbi->vbe_mode_info;
        uint16_t pitch  = *(uint16_t*)(mi + 16);
        uint16_t width  = *(uint16_t*)(mi + 18);
        uint16_t height = *(uint16_t*)(mi + 20);
        uint8_t  bpp    = *(uint8_t* )(mi + 25);
        uint32_t lfb    = *(uint32_t*)(mi + 40);
        if (bpp == 32 || bpp == 24 || bpp == 16) {
            fb_addr = (uint8_t*)lfb; fb_w = width; fb_h = height;
            fb_pitch_bytes = pitch; fb_bpp = bpp; fb_ok = 1;
        }
    }

    if (!fb_ok) { tty_puts("[fb] init failed\n"); return -1; }

    tty_puts("[ok] FB: "); tty_put_hex(fb_w);
    tty_puts("x"); tty_put_hex(fb_h);
    tty_puts("x"); tty_put_hex(fb_bpp);
    tty_puts(" @ "); tty_put_hex((uint32_t)fb_addr);
    tty_putc('\n');
    return 0;
}

int fb_backbuffer_init(void) {
    extern void* kmalloc(unsigned int);
    backbuf = (uint32_t*)kmalloc(fb_w * fb_h * 4);
    if (!backbuf) {
        tty_puts("[fb] backbuffer allocation FAILED (heap too small?)\n");
        return -1;
    }
    /* Копируем текущее состояние framebuffer в backbuf,
       чтобы уже напечатанный текст не потерялся. */
    for (uint32_t y = 0; y < fb_h; y++) {
        uint8_t* s = fb_addr + y * fb_pitch_bytes;
        uint32_t* d = backbuf + y * fb_w;
        for (uint32_t x = 0; x < fb_w; x++) {
            d[x] = s[x*4] | (s[x*4+1] << 8) | (s[x*4+2] << 16);
        }
    }
    tty_puts("[ok] Backbuffer @ "); tty_put_hex((uint32_t)backbuf); tty_putc('\n');
    return 0;
}

int      fb_ready(void)  { return fb_ok; }
uint32_t fb_width(void)  { return fb_w; }
uint32_t fb_height(void) { return fb_h; }
uint32_t fb_pitch(void)  { return fb_pitch_bytes; }

void fb_putpixel(uint32_t x, uint32_t y, uint32_t rgb) {
    if (!fb_ok || x >= fb_w || y >= fb_h) return;
    if (backbuf) {
        backbuf[y * fb_w + x] = rgb & 0x00FFFFFF;
        return;
    }
    uint8_t* p = fb_addr + y * fb_pitch_bytes + x * (fb_bpp / 8);
    p[0] = rgb & 0xFF;
    p[1] = (rgb >> 8) & 0xFF;
    p[2] = (rgb >> 16) & 0xFF;
    if (fb_bpp == 32) p[3] = 0;
}

uint32_t fb_getpixel(int x, int y) {
    if (!fb_ok || x < 0 || y < 0 || (uint32_t)x >= fb_w || (uint32_t)y >= fb_h)
        return 0;
    if (backbuf) return backbuf[y * fb_w + x];
    uint8_t* p = fb_addr + y * fb_pitch_bytes + x * (fb_bpp / 8);
    return p[0] | (p[1] << 8) | (p[2] << 16);
}

void fb_fill(uint32_t rgb) {
    for (uint32_t y = 0; y < fb_h; y++)
        for (uint32_t x = 0; x < fb_w; x++)
            fb_putpixel(x, y, rgb);
}

void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb) {
    for (uint32_t yy = y; yy < y + h && yy < fb_h; yy++)
        for (uint32_t xx = x; xx < x + w && xx < fb_w; xx++)
            fb_putpixel(xx, yy, rgb);
}

void fb_putc(char c, uint32_t x, uint32_t y, uint32_t rgb, int scale) {
    if (!fb_ok || scale < 1) return;
    if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
    const uint8_t* glyph = font8x8[(unsigned char)c - 32];
    for (int gy = 0; gy < 8; gy++) {
        uint8_t row = glyph[gy];
        for (int gx = 0; gx < 8; gx++) {
            if (row & (1 << (7 - gx))) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        fb_putpixel(x + gx*scale + sx, y + gy*scale + sy, rgb);
            }
        }
    }
}

void gfx_flush(void) {
    if (!fb_ok || !backbuf) return;
    for (uint32_t y = 0; y < fb_h; y++) {
        uint32_t* dst = (uint32_t*)(fb_addr + y * fb_pitch_bytes);
        uint32_t* src = backbuf + y * fb_w;
        uint32_t  cnt = fb_w;
        __asm__ volatile ("rep movsl"
                          : "+D"(dst), "+S"(src), "+c"(cnt)
                          : : "memory");
    }
}

/* ======== Примитивы ======== */

void gfx_clear(uint32_t rgb) { fb_fill(rgb); }

void gfx_box(int x, int y, int w, int h, uint32_t rgb) {
    for (int i = 0; i < w; i++) { fb_putpixel(x+i, y, rgb); fb_putpixel(x+i, y+h-1, rgb); }
    for (int i = 0; i < h; i++) { fb_putpixel(x, y+i, rgb); fb_putpixel(x+w-1, y+i, rgb); }
}

void gfx_box_fill(int x, int y, int w, int h, uint32_t rgb) {
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            fb_putpixel(x+xx, y+yy, rgb);
}

/* Bresenham */
void gfx_line(int x0, int y0, int x1, int y1, uint32_t rgb) {
    int dx =  (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fb_putpixel(x0, y0, rgb);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Midpoint circle (outline) */
void gfx_circle(int cx, int cy, int r, uint32_t rgb) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        fb_putpixel(cx+x, cy+y, rgb); fb_putpixel(cx+y, cy+x, rgb);
        fb_putpixel(cx-y, cy+x, rgb); fb_putpixel(cx-x, cy+y, rgb);
        fb_putpixel(cx-x, cy-y, rgb); fb_putpixel(cx-y, cy-x, rgb);
        fb_putpixel(cx+y, cy-x, rgb); fb_putpixel(cx+x, cy-y, rgb);
        y++;
        if (err < 0) err += 2*y + 1;
        else { x--; err += 2*(y - x) + 1; }
    }
}

void gfx_gradient_v(uint32_t top_rgb, uint32_t bot_rgb) {
    for (uint32_t y = 0; y < fb_h; y++) {
        uint32_t t = (y * 255) / (fb_h ? fb_h - 1 : 1);
        uint32_t r = ((top_rgb >> 16) & 0xFF) * (255 - t) / 255 + ((bot_rgb >> 16) & 0xFF) * t / 255;
        uint32_t g = ((top_rgb >>  8) & 0xFF) * (255 - t) / 255 + ((bot_rgb >>  8) & 0xFF) * t / 255;
        uint32_t b = ((top_rgb      ) & 0xFF) * (255 - t) / 255 + ((bot_rgb      ) & 0xFF) * t / 255;
        uint32_t c = (r << 16) | (g << 8) | b;
        for (uint32_t x = 0; x < fb_w; x++) backbuf[y * fb_w + x] = c;
    }
}

void gfx_puts(int x, int y, const char* s, uint32_t rgb, int scale) {
    while (*s) {
        fb_putc(*s, x, y, rgb, scale);
        x += 8 * scale;
        s++;
    }
}


void gfx_flush_rect(int x, int y, int w, int h) {
    if (!fb_ok || !backbuf) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)fb_w) w = (int)fb_w - x;
    if (y + h > (int)fb_h) h = (int)fb_h - y;
    if (w <= 0 || h <= 0) return;

    for (int row = 0; row < h; row++) {
        uint32_t* dst = (uint32_t*)(fb_addr + (y + row) * fb_pitch_bytes + x * 4);
        uint32_t* src = backbuf + (y + row) * fb_w + x;
        uint32_t cnt = (uint32_t)w;
        __asm__ volatile ("rep movsl"
                          : "+D"(dst), "+S"(src), "+c"(cnt)
                          : : "memory");
    }
}

void fb_scroll_up(int cell_h) {
    if (!fb_ok || cell_h <= 0 || (uint32_t)cell_h >= fb_h) return;
    uint32_t shift_bytes = (uint32_t)cell_h * fb_pitch_bytes;
    uint32_t total = (fb_h - cell_h) * fb_pitch_bytes;

    uint8_t* dst = fb_addr;
    uint8_t* src = fb_addr + shift_bytes;
    for (uint32_t i = 0; i < total; i++) dst[i] = src[i];

    uint8_t* tail = fb_addr + (fb_h - cell_h) * fb_pitch_bytes;
    for (uint32_t i = 0; i < shift_bytes; i++) tail[i] = 0;
}

/* ========== Material You: rounded rectangles ========== */

void gfx_rounded_fill(int x, int y, int w, int h, int r, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r > w/2) r = w/2;
    if (r > h/2) r = h/2;
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int dx = 0, dy = 0, corner = 0;
            if      (px < r && py < r)              { dx = r-1-px;  dy = r-1-py;  corner = 1; }
            else if (px >= w-r && py < r)           { dx = px-(w-r);dy = r-1-py;  corner = 1; }
            else if (px < r && py >= h-r)           { dx = r-1-px;  dy = py-(h-r);corner = 1; }
            else if (px >= w-r && py >= h-r)        { dx = px-(w-r);dy = py-(h-r);corner = 1; }
            if (corner) {
                if (dx*dx + dy*dy <= r*r)
                    fb_putpixel(x + px, y + py, c);
            } else {
                fb_putpixel(x + px, y + py, c);
            }
        }
    }
}

void gfx_rounded_outline(int x, int y, int w, int h, int r, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (r > w/2) r = w/2;
    if (r > h/2) r = h/2;
    /* Рисуем "кольцо": проверяем каждый пиксель, лежит ли он на границе */
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int on = 0;
            if (px < r && py < r) {
                int dx = r-1-px, dy = r-1-py;
                int d2 = dx*dx + dy*dy;
                if (d2 <= r*r && d2 > (r-1)*(r-1)) on = 1;
            } else if (px >= w-r && py < r) {
                int dx = px-(w-r), dy = r-1-py;
                int d2 = dx*dx + dy*dy;
                if (d2 <= r*r && d2 > (r-1)*(r-1)) on = 1;
            } else if (px < r && py >= h-r) {
                int dx = r-1-px, dy = py-(h-r);
                int d2 = dx*dx + dy*dy;
                if (d2 <= r*r && d2 > (r-1)*(r-1)) on = 1;
            } else if (px >= w-r && py >= h-r) {
                int dx = px-(w-r), dy = py-(h-r);
                int d2 = dx*dx + dy*dy;
                if (d2 <= r*r && d2 > (r-1)*(r-1)) on = 1;
            } else {
                if (px == 0 || px == w-1 || py == 0 || py == h-1) on = 1;
            }
            if (on) fb_putpixel(x + px, y + py, c);
        }
    }
}

/* Мягкая тень: несколько слоёв прямоугольников с нарастающей прозрачностью */
void gfx_shadow(int x, int y, int w, int h, int r, uint32_t bg) {
    /* Очень тонкая — 2 слоя по 1 пикселю */
    (void)bg;
    for (int i = 1; i <= 3; i++) {
        uint32_t c = (i == 1) ? 0x0A0A0C : ((i == 2) ? 0x08080A : 0x060608);
        gfx_rounded_outline(x - i, y - i, w + 2*i, h + 2*i, r + i, c);
    }
}

