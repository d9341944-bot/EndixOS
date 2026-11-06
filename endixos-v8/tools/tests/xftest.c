#include "gui/wm.c"
typedef struct _IO_FILE FILE;
extern void *mmap(void *, unsigned long, int, int, int, long); extern int printf(const char *, ...);
void sys_reboot(void) {} void sys_shutdown(void) {}
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static u32 *vram; static u32 T = 5000;
static void ticks(int n) { for (int i = 0; i < n; i++) { T += 2; now_ticks = T; wm_tick(T); } }
static u32 lum(u32 c) { return ((c >> 16 & 255) + (c >> 8 & 255) + (c & 255)) / 3; }
int main(void)
{
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32);
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); ticks(30);
    open_window(APP_FILES); ticks(80);
    wm_render(); fb_flip();
    u32 px = 0; i32 X = wins[APP_FILES].x + 300, Y = wins[APP_FILES].y + 200;   /* белый фон окна */
    u32 l0 = lum(vram[Y * 1024 + X]);
    theme_set_dark(1);
    wm_render(); fb_flip();
    u32 l1 = lum(vram[Y * 1024 + X]);
    CHECK(xf_on && l1 > l0 - 15, "кадр сразу после смены темы ещё почти старый");
    ticks(10); wm_render(); fb_flip();
    u32 l2 = lum(vram[Y * 1024 + X]);
    CHECK(l2 < l1 && l2 > 40, "в середине перехода — промежуточный цвет");
    ticks(40); wm_render(); fb_flip();
    u32 l3 = lum(vram[Y * 1024 + X]);
    CHECK(!xf_on && l3 < l2, "переход закончился, тёмная тема");
    printf("l0=%u l1=%u l2=%u l3=%u\n", l0, l1, l2, l3);
    (void)px; printf("fails=%d\n", fails); return fails;
}
