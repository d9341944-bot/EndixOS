#include "gui/files.c"
#include "gui/wm.c"
extern void *mmap(void *, unsigned long, int, int, int, long); extern int printf(const char *, ...); extern int snprintf(char *, unsigned long, const char *, ...);
void sys_reboot(void) {} void sys_shutdown(void) {}
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static u32 *vram; static u32 T = 5000;
static void ticks(int n) { for (int i = 0; i < n; i++) { T += 2; now_ticks = T; wm_tick(T); } }
static void mouse(i32 x, i32 y, i32 b) { event_t e = { EV_MOUSE, x, y, b }; wm_handle_event(&e); ticks(2); }
static void wheel(int d) { event_t e = { EV_WHEEL, d, 0, 0 }; wm_handle_event(&e); ticks(2); }
int main(void)
{
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32);
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); ticks(30);
    int d = fs_mkdir(fs_home(), "many"); char nm[16];
    for (int i = 0; i < 60; i++) { snprintf(nm, sizeof nm, "f%02d.txt", i); fs_create(d, nm); }
    open_window(APP_FILES); ticks(80); files_goto(d);
    window_t *w = &wins[APP_FILES]; mouse(w->x + 300, w->y + 200, 0);
    CHECK(scroll == 0, "в начале");
    wheel(-1); CHECK(scroll == 1, "колесо вниз прокручивает на строку");
    wheel(-1); wheel(-1); CHECK(scroll == 3, "ещё вниз");
    wheel(1); CHECK(scroll == 2, "колесо вверх");
    for (int i = 0; i < 100; i++) wheel(-1);
    CHECK(scroll == (nitems + ncols() - 1) / ncols() - nrows(), "не дальше конца");
    for (int i = 0; i < 100; i++) wheel(1);
    CHECK(scroll == 0, "не выше начала");
    open_window(APP_TERMINAL); ticks(80); w = &wins[APP_TERMINAL]; mouse(w->x + 100, w->y + 100, 0);

    wheel(1); CHECK(tview >= 0, "терминал принимает колесо");
    printf("fails=%d\n", fails); return fails;
}
