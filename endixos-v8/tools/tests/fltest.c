#include "gui/files.c"
#include "gui/wm.c"
typedef struct _IO_FILE FILE;
extern FILE *fopen(const char *, const char *); extern unsigned fwrite(const void *, unsigned, unsigned, FILE *); extern int fclose(FILE *);
extern void *mmap(void *, unsigned long, int, int, int, long); extern int printf(const char *, ...); extern int snprintf(char *, unsigned long, const char *, ...);
void sys_reboot(void) {} void sys_shutdown(void) {}
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static u32 *vram; static u32 T = 5000;
static void ticks(int n) { for (int i = 0; i < n; i++) wm_tick(T += 2); }
static void mouse(i32 x, i32 y, i32 b) { event_t e = { EV_MOUSE, x, y, b }; wm_handle_event(&e); ticks(2); }
static void click(i32 x, i32 y) { mouse(x, y, 0); mouse(x, y, 1); mouse(x, y, 0); ticks(40); }
static void shot(const char *n) { char p[128]; wm_render(); fb_flip(); snprintf(p, sizeof p, "fl_%s.raw", n); FILE *f = fopen(p, "wb"); fwrite(vram, 4, 1024 * 768, f); fclose(f); }
int main(void)
{
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32);
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); ticks(30);
    open_window(APP_FILES); ticks(60);
    i32 ox = wins[APP_FILES].x, oy = wins[APP_FILES].y + TITLE_H;
    CHECK(listv == 0, "по умолчанию значки");
    click(ox + VIEW_X + 45, oy + 18); CHECK(listv == 1, "переключатель: список");
    shot("list");
    int g0 = grid_y(); i32 ry = g0 + 2 * ROW_H + 5;
    click(ox + SIDE_W + 60, oy + ry); CHECK(sel >= 0 && sel == items[2], "клик по строке выбирает третий элемент");

    files_key(KEY_DOWN); CHECK(sel == items[3], "стрелка вниз");
    click(ox + VIEW_X + 12, oy + 18); CHECK(listv == 0, "обратно значки");
    click(ox + 20, oy + 34 + 7 * 30 + 28 + 6); CHECK(cur == fs_trash(), "боковая панель: Trash");
    shot("trash");
    printf("fails=%d\n", fails); return fails;
}
