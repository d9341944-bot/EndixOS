#include "gui/wm.c"
extern void *mmap(void *, unsigned long, int, int, int, long); extern int printf(const char *, ...);
void sys_reboot(void) {} void sys_shutdown(void) {}
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static u32 *vram; static u32 T = 5000;
static void ticks(int n) { for (int i = 0; i < n; i++) { T += 2; now_ticks = T; wm_tick(T); } }
static void mouse(i32 x, i32 y, i32 b) { event_t e = { EV_MOUSE, x, y, b }; wm_handle_event(&e); ticks(2); }
int main(void)
{
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32);
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); ticks(30);
    i32 ix, iy; dock_icon_pos(APP_FILES, &ix, &iy);
    mouse(ix + 20, iy + 20, 0);
    CHECK(lbl_v > 0 && lbl_v < 1024, "подпись Dock появляется плавно");
    ticks(40); CHECK(lbl_v == 1024 && lbl_id == APP_FILES, "подпись показана полностью");
    mouse(500, 300, 0); ticks(40); CHECK(lbl_v == 0, "подпись плавно исчезает");
    open_window(APP_FILES); ticks(80);
    window_t *w = &wins[APP_FILES];
    mouse(w->x + 200, w->y + 10, 0); mouse(w->x + 200, w->y + 10, 1); mouse(w->x + 230, w->y + 30, 1); ticks(10);
    CHECK(drag_app == APP_FILES && lift_v > 0, "перетаскиваемое окно приподнимается");
    mouse(w->x + 230, w->y + 30, 0); ticks(60);
    CHECK(lift_v == 0, "после отпускания тень возвращается");
    printf("fails=%d\n", fails); return fails;
}
