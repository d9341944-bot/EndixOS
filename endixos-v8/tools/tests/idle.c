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
    int apps[] = { -1, APP_NOTES, APP_TERMINAL, APP_FILES, APP_SETTINGS, APP_CLOCK, APP_BROWSER, APP_MUSIC, APP_CAL, APP_SNAKE };
    for (int k = 0; k < 10; k++) {
        for (int a = 0; a < WIN_COUNT; a++) wins[a].open = 0;
        if (apps[k] >= 0) { open_window(apps[k]); }
        for (int i = 0; i < 100; i++) { T += 2; now_ticks = T; if (wm_tick(T)) scene_dirty = 1; wm_frame(T); }
        int n = 0;
        for (int i = 0; i < 500; i++) { T += 2; now_ticks = T; if (wm_tick(T)) scene_dirty = 1; wm_frame(T); n += wm_last_was_scene(); }
        printf("app %2d: %d перерисовок за 10 с\n", apps[k], n);
    }
    printf("fails=%d\n", fails); return fails;
}
