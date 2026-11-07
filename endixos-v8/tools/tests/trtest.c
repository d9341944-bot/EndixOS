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
int main(int argc, char **argv)
{
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32);
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); ticks(30);
    int t = fs_trash();
    CHECK(t >= 0 && fs_count(t) == 0, "Корзина пуста");
    int d = fs_lookup(fs_home(), "Desktop");
    int n = fs_create(d, "a.txt"); fs_create(d, "b.txt");
    CHECK(fs_trash_put(n) == 0 && fs_count(t) == 1 && fs_lookup(t, "a.txt") >= 0, "файл ушёл в Корзину");
    int m = fs_create(d, "a.txt");
    CHECK(fs_trash_put(m) == 0 && fs_count(t) == 2 && fs_lookup(t, "a 2.txt") >= 0, "одноимённый получает новое имя");
    CHECK(fs_in_trash(fs_lookup(t, "a.txt")) && !fs_in_trash(d), "fs_in_trash");
    CHECK(fs_trash_put(t) != 0, "саму Корзину в Корзину нельзя");
    /* значок на рабочем столе: выбрать и нажать Delete */
    int b = fs_lookup(d, "b.txt"); desktop_select(b);
    event_t e = { EV_KEY, KEY_DELETE, 0, 0 }; wm_handle_event(&e); ticks(5);
    CHECK(fs_lookup(d, "b.txt") < 0 && fs_lookup(t, "b.txt") >= 0, "Delete на рабочем столе кладёт в Корзину");
    /* Dock: Корзина в последнем слоте, клик открывает Files в Корзине */
    i32 ix, iy; dock_icon_pos(TRASH_ID, &ix, &iy);
    CHECK(dock_hit(ix + 10, iy + 10) == TRASH_ID, "Dock: попадание в Корзину");
    click(ix + 10, iy + 10);
    CHECK(wins[APP_FILES].open, "клик по Корзине открывает Files");
    wm_render(); fb_flip();
    { FILE *f = fopen("trash.raw", "wb"); fwrite(vram, 4, 1024 * 768, f); fclose(f); }
    CHECK(fs_trash_empty() == 3 && fs_count(t) == 0, "Empty Trash очищает");
    ticks(10); wm_render();
    printf("fails=%d\n", fails); return fails;
}
