#include "gui/wm.c"
static void settle(void) { u32 s0 = now_ticks; for (int i = 1; i <= 40; i++) wm_tick(s0 + 2 * i); wm_tick(s0); }
extern void *mmap(void *, unsigned long, int, int, int, long);
extern int printf(const char *, ...);
typedef struct _IO_FILE FILE;
extern FILE *fopen(const char *, const char *); extern unsigned fwrite(const void *, unsigned, unsigned, FILE *); extern int fclose(FILE *);
void sys_reboot(void) {} void sys_shutdown(void) {}
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static u32 *vram;
static void mouse(i32 x, i32 y, i32 b) { event_t e = { EV_MOUSE, x, y, b }; wm_handle_event(&e); settle(); }
static void click(i32 x, i32 y) { mouse(x, y, 0); mouse(x, y, 1); mouse(x, y, 0); }
static void key(i32 c) { event_t e = { EV_KEY, c, 0, 0 }; wm_handle_event(&e); settle(); }
static void shot(const char *n) { wm_render(); fb_flip(); FILE *f = fopen(n, "wb"); fwrite(vram, 4, 1024*768, f); fclose(f); }
int main(int argc, char **argv)
{
    vram = mmap(0, 1024*768*4, 3, 0x62, -1, 0);
    fb_init((u32)(unsigned long)vram, 4096, 1024, 768, 32);
    static sysinfo_t info = { "CPU", 128, 1024, 768 };
    wm_init(&info); wm_set_time(10, 31, 24, 10, 2026);
    wm_render();
    i32 ix, iy; dock_icon_pos(LP_ID, &ix, &iy);
    click(ix + 20, iy + 20);
    CHECK(lp_open, "клик по Launchpad в Dock открывает");
    int list[APP_COUNT]; CHECK(lp_list(list) == APP_COUNT, "все приложения в списке");
    i32 x, y; lp_cell(3, APP_COUNT, &x, &y);
    mouse(x + 80, y + 40, 0);
    shot(argv[1]);
    wins[APP_SNAKE].open = 0;
    click(x + 80, y + 40);
    CHECK(!lp_open && wins[APP_SNAKE].open, "клик по значку запускает приложение");
    click(ix + 20, iy + 20); key('t'); key('e'); key('r');
    CHECK(lp_list(list) == 1 && list[0] == APP_TERMINAL, "поиск 'ter' -> Terminal");
    key('\\b'); key('\\b'); key('z');
    CHECK(lp_list(list) == 0, "поиск 'z' пусто");
    shot(argv[2]);
    key(27);
    CHECK(!lp_open, "Esc закрывает");
    click(ix + 20, iy + 20); click(10, 400);
    CHECK(!lp_open, "клик по пустому месту закрывает");
    wm_render();
    printf("fails=%d\n", fails); return fails;
}
