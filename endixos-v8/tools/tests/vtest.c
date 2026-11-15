#include "gui/clockapp.c"
#include "gui/calapp.c"
#include "gui/wm.c"
#include "pic.h"
typedef struct _IO_FILE FILE;
extern FILE *fopen(const char *, const char *); extern unsigned fwrite(const void *, unsigned, unsigned, FILE *); extern int fclose(FILE *);
extern void *mmap(void *, unsigned long, int, int, int, long); extern int printf(const char *, ...); extern int snprintf(char *, unsigned long, const char *, ...);
void sys_reboot(void) {} void sys_shutdown(void) {}
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static u32 *vram; static u32 T = 5000;
static void ticks(int n) { for (int i = 0; i < n; i++) wm_tick(T += 2); }
static void mouse(i32 x, i32 y, i32 b) { event_t e = { EV_MOUSE, x, y, b }; wm_handle_event(&e); ticks(2); }
static void cl(i32 x, i32 y) { mouse(x, y, 0); mouse(x, y, 1); mouse(x, y, 0); }
static void rclick(i32 x, i32 y) { mouse(x, y, 0); mouse(x, y, 2); mouse(x, y, 0); }
static void key(i32 k, int ctrl) { event_t e = { EV_KEY, k, ctrl, 0 }; wm_handle_event(&e); ticks(2); }
static void shot(const char *n) { char p[128]; wm_render(); fb_flip(); snprintf(p, sizeof p, "vt_%s.raw", n); FILE *f = fopen(p, "wb"); fwrite(vram, 4, 1024 * 768, f); fclose(f); }
static int find(const char *n) { return fs_lookup(fs_home(), n); }
int main(void)
{
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32);
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); ticks(30);
    open_window(APP_CLOCK); ticks(40);
    i32 cx0 = wins[APP_CLOCK].x, cy0 = wins[APP_CLOCK].y + TITLE_H;
    cl(cx0 + 270, cy0 + 166);   /* Start */
    ticks(120);
    CHECK(sw_run, "секундомер идёт");
    cl(cx0 + 90, cy0 + 166);   /* Lap */
    CHECK(nlap == 1, "круг записан");
    shot("clock");
    cl(cx0 + 270, cy0 + 166);   /* Stop */
    key('	', 0); add_time(65); ticks(2);
    CHECK(tm_left == 65, "таймер: 65 секунд");
    key(' ', 0); ticks(3); shot("timer");
    ticks(7000);
    CHECK(tm_fired >= 1 && !tm_run, "таймер сработал");
    open_window(APP_CAL); ticks(40);
    cal_set_today(2026, 11, 11);
    shot("cal");
    key(KEY_RIGHT, 0); CHECK(vm == 12, "месяц вперёд");
    printf("fails=%d\n", fails);
    return fails;
}
