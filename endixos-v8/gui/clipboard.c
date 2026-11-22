#include "clipboard.h"
#include "fb.h"

static u32 img[CLIP_MAXW * CLIP_MAXH];
static int iw, ih;

void clip_copy_screen(void)
{
    i32 W = (i32)fb_width(), H = (i32)fb_height();
    const u32 *px = fb_buffer();
    if (W <= 0 || H <= 0) return;
    i32 dw = W, dh = H;
    if (dw > CLIP_MAXW) { dh = dh * CLIP_MAXW / dw; dw = CLIP_MAXW; }
    if (dh > CLIP_MAXH) { dw = dw * CLIP_MAXH / dh; dh = CLIP_MAXH; }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    for (i32 y = 0; y < dh; y++) {
        i32 y0 = y * H / dh, y1 = (y + 1) * H / dh; if (y1 <= y0) y1 = y0 + 1;
        for (i32 x = 0; x < dw; x++) {
            i32 x0 = x * W / dw, x1 = (x + 1) * W / dw; if (x1 <= x0) x1 = x0 + 1;
            u32 r = 0, g = 0, b = 0, n = 0;
            for (i32 sy = y0; sy < y1; sy++)
                for (i32 sx = x0; sx < x1; sx++) { u32 c = px[sy * W + sx]; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++; }
            img[y * dw + x] = ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
    iw = dw; ih = dh;
}
int clip_has_image(void) { return iw > 0; }
int clip_image_w(void) { return iw; }
int clip_image_h(void) { return ih; }
const u32 *clip_image(void) { return img; }
