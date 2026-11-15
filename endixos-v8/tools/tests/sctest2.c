#include "gui/fb.c"
extern int printf(const char *, ...); extern void *mmap(void *, unsigned long, int, int, int, long);
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
/* эталон: усреднение по площади, как раньше */
static u32 ref_px(const u32 *src, u32 pw, u32 lw, u32 dx)
{
    u32 a = dx * lw * 256 / pw, b = (dx + 1) * lw * 256 / pw; u64 r = 0, g = 0, bl = 0;
    for (u32 px = a >> 8; px <= (b - 1) >> 8; px++) {
        u32 ps = px << 8, pe = ps + 256, c = (pe < b ? pe : b) - (ps > a ? ps : a);
        r += (u64)((src[px] >> 16) & 255) * c; g += (u64)((src[px] >> 8) & 255) * c; bl += (u64)(src[px] & 255) * c;
    }
    u32 t = b - a; return (u32)(r / t) << 16 | (u32)(g / t) << 8 | (u32)(bl / t);
}
static void test(u32 pw, u32 lw)
{
    static u32 vram[1920 * 8]; u32 bad = 0, worst = 0;
    fb_init((u32)(unsigned long)vram, 1920 * 4, pw, 4, 32);
    fb_set_logical_width(lw);
    for (u32 y = 0; y < 4; y++) for (u32 x = 0; x < lw; x++) backbuf[y * lw + x] = (x * 37 + y * 11) * 0x010203u ^ (x * 91 << 8);
    fb_flip();
    for (u32 dx = 0; dx < pw; dx++) {
        u32 e = ref_px(backbuf, pw, lw, dx), g = vram[dx];
        for (int s = 0; s < 24; s += 8) { int d = (int)((e >> s) & 255) - (int)((g >> s) & 255); if (d < 0) d = -d; if ((u32)d > worst) worst = (u32)d; if (d > 2) bad++; }
    }
    printf("      %ux%u: макс. отличие %u\n", pw, lw, worst);
    CHECK(bad == 0, "сжатие совпадает с эталоном (допуск 2)");
}
int main(void)
{
    test(800, 1024); test(1024, 1366); test(800, 1280); test(640, 1280);
    static u32 vram2[1024 * 4]; fb_init((u32)(unsigned long)vram2, 4096, 1024, 4, 32);
    for (u32 i = 0; i < 1024 * 4; i++) backbuf[i] = 0xFFFFFF; fb_perf_set(0); fb_shadow(100, 1, 50, 2, 1, 10, 200);
    fb_perf_set(1); u32 before = backbuf[90]; fb_shadow(100, 1, 50, 2, 1, 10, 200);
    CHECK(fb_perf_get() == 1 && backbuf[90] == before, "на уровне 1 тени отключены");
    return fails;
}
