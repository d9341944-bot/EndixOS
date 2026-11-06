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
static void rclick(i32 x, i32 y) { mouse(x, y, 0); mouse(x, y, 2); mouse(x, y, 0); ticks(40); }
static void click(i32 x, i32 y) { mouse(x, y, 0); mouse(x, y, 1); mouse(x, y, 0); ticks(40); }
static void shot(const char *n) { char p[128]; wm_render(); fb_flip(); snprintf(p, sizeof p, "dm_%s.raw", n); FILE *f = fopen(p, "wb"); fwrite(vram, 4, 1024 * 768, f); fclose(f); }
int main(void)
{
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32);
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); ticks(30);
    i32 ix, iy;
    dock_icon_pos(APP_FILES, &ix, &iy); mouse(ix + 20, iy + 20, 0); dock_icon_pos(APP_FILES, &ix, &iy);
    rclick(ix + 20, iy + 20);
    CHECK(dm_id == APP_FILES && dm_n >= 1, "правая кнопка по значку открывает меню");
    CHECK(!strcmp(dm_lbl[0], "Open") || !wins[APP_FILES].open, "у закрытого приложения Open");
    click(dm_x + 30, dm_y + 6 + 5);
    CHECK(dm_id < 0 && wins[APP_FILES].open, "Open запускает приложение");
    dock_icon_pos(APP_FILES, &ix, &iy); mouse(ix + 20, iy + 20, 0);
    rclick(ix + 20, iy + 20); shot("menu");
    CHECK(dm_n == 2 && !strcmp(dm_lbl[1], "Quit"), "у открытого: Minimize / Quit");
    click(dm_x + 30, dm_y + 6 + DM_IH + 5);
    CHECK(!wins[APP_FILES].open, "Quit закрывает окно");
    fs_create(fs_home(), "tmp1.txt"); fs_trash_put(fs_lookup(fs_home(), "tmp1.txt"));
    dock_icon_pos(TRASH_ID, &ix, &iy); mouse(ix + 20, iy + 20, 0); dock_icon_pos(TRASH_ID, &ix, &iy);
    rclick(ix + 20, iy + 20);
    click(dm_x + 30, dm_y + 6 + DM_IH + 5);
    CHECK(fs_count(fs_trash()) == 0, "Empty Trash в меню Корзины");
    rclick(ix + 20, iy + 20); click(300, 300); CHECK(dm_id < 0, "клик мимо закрывает меню");
    printf("fails=%d\n", fails); return fails;
}
