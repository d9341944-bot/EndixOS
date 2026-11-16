struct timespec { long tv_sec; long tv_nsec; }; extern int clock_gettime(int, struct timespec *);
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
    struct timespec t0, t1;
#define TIME(name, setup) do { setup; wm_render(); clock_gettime(1, &t0); for (int q = 0; q < 20; q++) { scene_dirty = 1; wm_render(); } clock_gettime(1, &t1); printf("%-14s %6.2f ms/frame\n", name, ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / 20); } while (0)
    TIME("desktop", (void)0);
    open_window(APP_FILES); ticks(30); TIME("files", (void)0);
    open_window(APP_SETTINGS); ticks(30); TIME("settings", (void)0);
    open_window(APP_BROWSER); ticks(30); TIME("browser", (void)0);
    open_window(APP_NOTES); open_window(APP_TERMINAL); ticks(30); TIME("5 windows", (void)0);
    printf("fails=%d\n", fails); return fails;
}
