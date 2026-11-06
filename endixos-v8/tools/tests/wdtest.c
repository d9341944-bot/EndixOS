#include "gui/wm.c"
typedef struct _IO_FILE FILE;
extern FILE *fopen(const char *, const char *); extern unsigned fwrite(const void *, unsigned, unsigned, FILE *); extern int fclose(FILE *);
extern void *mmap(void *, unsigned long, int, int, int, long); extern int printf(const char *, ...); extern int snprintf(char *, unsigned long, const char *, ...);
void sys_reboot(void) {} void sys_shutdown(void) {}
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static u32 *vram; static const char *prefix; static u32 T = 5000;
static void frame(const char *n) { char p[256]; wm_render(); fb_flip(); snprintf(p, sizeof p, "%s_%s.raw", prefix, n); FILE *f = fopen(p, "wb"); fwrite(vram, 4, 1024 * 768, f); fclose(f); }
static void ticks(int n) { for (int i = 0; i < n; i++) wm_tick(T += 2); }
static void mouse(i32 x, i32 y, i32 b) { event_t e = { EV_MOUSE, x, y, b }; wm_handle_event(&e); ticks(2); }
static void drag(i32 x0, i32 y0, i32 x1, i32 y1)
{
    mouse(x0, y0, 0); mouse(x0, y0, 1);
    for (int k = 1; k <= 8; k++) mouse(x0 + (x1 - x0) * k / 8, y0 + (y1 - y0) * k / 8, 1);
    frame("mid");
    mouse(x1, y1, 0); ticks(80);
}
int main(int argc, char **argv)
{
    prefix = argv[1];
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    if (fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32)) return 2;
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); wm_set_time(16, 35, 2, 11, 2026);
    u8 p0[8]; widgets_get_pos(p0);
    printf("start pos: clock %d,%d cal %d,%d sys %d,%d\n", p0[0]*8, p0[1]*8, p0[2]*8, p0[3]*8, p0[4]*8, p0[5]*8);
    widgets_edit_start(); ticks(80); frame("e0");
    drag(100, 100, 480, 300);                       /* часы -> в центр */
    u8 p1[8]; widgets_get_pos(p1);
    printf("after: clock %d,%d\n", p1[0]*8, p1[1]*8);
    CHECK(p1[0]*8 > 300 && p1[1]*8 > 150, "часы перенесены");
    drag(100, 250, 480, 300);                       /* календарь на часы: вернётся */
    u8 p2[8]; widgets_get_pos(p2);
    CHECK(p2[2] == p0[2] && p2[3] == p0[3], "наложение: виджет вернулся на место");
    drag(100, 250, 300, 740);                       /* под Dock: вернётся */
    widgets_get_pos(p2);
    CHECK(p2[2] == p0[2] && p2[3] == p0[3], "под Dock: вернулся");
    drag(100, 250, 640, 470);
    widgets_get_pos(p2); CHECK(p2[2]*8 > 450, "календарь перенесён вправо");
    CHECK(widgets_pos_dirty() == 1 && widgets_pos_dirty() == 0, "pos_dirty взводится один раз");
    drag(640, 470, 800, 100);
    { u8 q[8]; widgets_get_pos(q); CHECK(q[2]*8 > 450 && q[2]*8 < 700, "под галереей: вернулся"); }
    frame("e1");
    u8 save[8]; widgets_get_pos(save);
    widgets_set_mask(0); widgets_set_mask(7); widgets_set_pos(save);
    u8 p3[8]; widgets_get_pos(p3);
    int same = 1; for (int i = 0; i < 6; i++) if (p3[i] != save[i]) same = 0;
    CHECK(same, "сохранение/загрузка мест");
    widgets_edit_stop(); ticks(80); frame("e2");
    printf("fails=%d\n", fails); return fails;
}
