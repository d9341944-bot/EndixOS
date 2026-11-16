#include "gui/wm.c"
typedef struct _IO_FILE FILE;
extern FILE *fopen(const char *, const char *); extern unsigned fwrite(const void *, unsigned, unsigned, FILE *); extern int fclose(FILE *);
extern void *mmap(void *, unsigned long, int, int, int, long); extern int printf(const char *, ...); extern int snprintf(char *, unsigned long, const char *, ...);
void sys_reboot(void) {} void sys_shutdown(void) {}
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static u32 *vram; static u32 T = 5000;
static void ticks(int n) { for (int i = 0; i < n; i++) { T += 2; now_ticks = T; wm_tick(T); } }
static void mouse(i32 x, i32 y, i32 b) { event_t e = { EV_MOUSE, x, y, b }; wm_handle_event(&e); ticks(2); }
static void shot(const char *n) { char p[128]; wm_render(); fb_flip(); snprintf(p, sizeof p, "nt_%s.raw", n); FILE *f = fopen(p, "wb"); fwrite(vram, 4, 1024 * 768, f); fclose(f); }
int main(void)
{
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32);
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); ticks(30); mouse(300, 500, 0);
    open_window(APP_NOTES); ticks(40); wm_render(); fb_flip();
    u32 f0 = fb_flipped_bytes(); wm_render(); fb_flip(); u32 same = fb_flipped_bytes() - f0;
    CHECK(same == 0, "неизменный кадр не пишет в видеопамять");
    f0 = fb_flipped_bytes(); notes_key('a', 0); ticks(4); wm_render(); fb_flip(); u32 typed = fb_flipped_bytes() - f0;
    printf("ввод буквы: %u байт из %u\n", typed, 1024 * 768 * 4);
    CHECK(typed < 1024 * 768 * 4 / 4, "ввод буквы пишет малую часть экрана");
    wm_render(); fb_flip(); { u32 *a = vram; static u32 ref[1024 * 768]; for (int i = 0; i < 1024 * 768; i++) ref[i] = a[i]; extern void fb_flip_invalidate(void); fb_flip_invalidate(); fb_flip(); int bad = 0; for (int i = 0; i < 1024 * 768; i++) bad += ref[i] != a[i]; CHECK(bad == 0, "экран после дифф-flip совпадает с полным"); }
    printf("fails=%d\n", fails); return fails;
}
