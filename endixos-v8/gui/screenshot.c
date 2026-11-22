#include "screenshot.h"
#include "fb.h"
#include "photos.h"
#include "../kernel/blk.h"

static u8 buf[FB_MAX_W * FB_MAX_H * 3 + FB_MAX_H + 65536 / 8 * 0 + (FB_MAX_W * FB_MAX_H * 3 / 65535 + 2) * 5 + 128];
static u32 crct[256]; static int crc_ok, seq;

static u32 crc_upd(u32 c, const u8 *d, u32 n)
{
    if (!crc_ok) { for (u32 i = 0; i < 256; i++) { u32 x = i; for (int k = 0; k < 8; k++) x = x & 1 ? 0xEDB88320u ^ (x >> 1) : x >> 1; crct[i] = x; } crc_ok = 1; }
    for (u32 i = 0; i < n; i++) c = crct[(c ^ d[i]) & 255] ^ (c >> 8);
    return c;
}
static u8 *be32(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; return p + 4; }
static u8 *chunk_end(u8 *start, u8 *p)                   /* start - начало длины; p - конец данных */
{
    be32(start, (u32)(p - start - 8));
    return be32(p, ~crc_upd(0xFFFFFFFFu, start + 4, (u32)(p - start - 4)));
}

int screenshot_take(char *name, int cap)
{
    i32 w = (i32)fb_width(), h = (i32)fb_height();
    const u32 *px = fb_buffer();
    u8 *p = buf;
    static const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    for (int i = 0; i < 8; i++) *p++ = sig[i];
    u8 *c = p; p += 4; *p++ = 'I'; *p++ = 'H'; *p++ = 'D'; *p++ = 'R';
    p = be32(p, (u32)w); p = be32(p, (u32)h); *p++ = 8; *p++ = 2; *p++ = 0; *p++ = 0; *p++ = 0;
    p = chunk_end(c, p);
    u32 raw = (u32)h * (3 * (u32)w + 1);
    c = p; p += 4; *p++ = 'I'; *p++ = 'D'; *p++ = 'A'; *p++ = 'T';
    *p++ = 0x78; *p++ = 0x01;
    u32 a = 1, b = 0, left = raw, row = 0, col = 0; int phase = 0;   /* phase: 0 - байт фильтра, 1 - пиксели */
    u32 sub = 0;
    (void)sub;
    while (left) {
        u32 n = left > 65535 ? 65535 : left;
        *p++ = n == left; *p++ = n; *p++ = n >> 8; *p++ = ~n; *p++ = (~n) >> 8;
        for (u32 i = 0; i < n; i++) {
            u8 v;
            if (phase == 0) { v = 0; phase = 1; col = 0; }
            else {
                u32 q = px[row * (u32)w + col / 3];
                u32 ch = col % 3;
                v = ch == 0 ? (u8)(q >> 16) : ch == 1 ? (u8)(q >> 8) : (u8)q;
                if (++col == 3 * (u32)w) { phase = 0; row++; }
            }
            *p++ = v; a += v; if (a >= 65521) a -= 65521; b += a; if (b >= 65521) b -= 65521;
        }
        left -= n;
    }
    p = be32(p, (b << 16) | a);
    p = chunk_end(c, p);
    c = p; p += 4; *p++ = 'I'; *p++ = 'E'; *p++ = 'N'; *p++ = 'D';
    p = chunk_end(c, p);
    static fat_t f;
    if (!photos_volume(&f)) { const char *m = "No disk"; int i = 0; for (; m[i]; i++) name[i] = m[i]; name[i] = 0; return -1; }
    fat_mkdir(&f, "/Pictures");
    char path[40] = "/Pictures/";
    for (int tries = 0; tries < 50; tries++) {
        seq++;
        char *q = path + 10; *q++ = 'S'; *q++ = 'H'; *q++ = 'O'; *q++ = 'T';
        for (int d = 3, v = seq; d >= 0; d--, v /= 10) q[d] = (char)('0' + v % 10);
        q += 4; *q++ = '.'; *q++ = 'P'; *q++ = 'N'; *q++ = 'G'; *q = 0;
        u8 t;
        if (fat_read(&f, path, &t, 1) <= 0) break;       /* имя свободно */
    }
    if (fat_write(&f, path, buf, (u32)(p - buf)) != 0) { const char *m = "Could not write the screenshot"; int i = 0; for (; m[i]; i++) name[i] = m[i]; name[i] = 0; return -1; }
    int i = 0; for (const char *s = path + 10; *s && i < cap - 1; s++) name[i++] = *s; name[i] = 0;
    return 0;
}
