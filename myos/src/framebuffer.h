#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H
#include <stdint.h>
#include "multiboot.h"

int      fb_init(struct multiboot_info* mbi);
int      fb_ready(void);
uint32_t fb_width(void);
uint32_t fb_height(void);
uint32_t fb_pitch(void);

void     fb_putpixel(uint32_t x, uint32_t y, uint32_t rgb);
uint32_t fb_getpixel(int x, int y);
void     fb_fill(uint32_t rgb);
void     fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb);

/* текст 8x8 (scale по умолчанию 2 => 16x16) */
void     fb_putc(char c, uint32_t x, uint32_t y, uint32_t rgb, int scale);
void     fb_scroll_up(int cell_h);

/* Двойная буферизация */
int      fb_backbuffer_init(void);
void     gfx_flush(void);
void     gfx_flush_rect(int x, int y, int w, int h);

/* Примитивы (пишут в back buffer, если он есть) */
void     gfx_clear(uint32_t rgb);
void     gfx_box  (int x, int y, int w, int h, uint32_t rgb);
void     gfx_box_fill(int x, int y, int w, int h, uint32_t rgb);
void     gfx_line (int x0, int y0, int x1, int y1, uint32_t rgb);
void     gfx_circle(int cx, int cy, int r, uint32_t rgb);
void     gfx_gradient_v(uint32_t top_rgb, uint32_t bot_rgb);
void     gfx_puts(int x, int y, const char* s, uint32_t rgb, int scale);

/* Material You helpers */
void     gfx_rounded_fill(int x, int y, int w, int h, int r, uint32_t c);
void     gfx_rounded_outline(int x, int y, int w, int h, int r, uint32_t c);
void     gfx_shadow(int x, int y, int w, int h, int r, uint32_t bg);

#endif
