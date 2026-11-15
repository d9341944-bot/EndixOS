#include "wav.h"
#include "gui/clockapp.c"
#include "gui/calapp.c"
#include "gui/music.c"

#include "gui/wm.c"
#include "pic.h"
typedef struct _IO_FILE FILE;
extern FILE *fopen(const char *, const char *); extern unsigned fwrite(const void *, unsigned, unsigned, FILE *); extern int fclose(FILE *);
extern void *mmap(void *, unsigned long, int, int, int, long); extern int printf(const char *, ...); extern int snprintf(char *, unsigned long, const char *, ...);
void sys_reboot(void) {} void sys_shutdown(void) {}
static int fails; static u8 ECM[256];
int ec_read(u8 o, u8 *v) { *v = ECM[o]; return 0; }
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
    CHECK(scenery_count() == 12, "в паке 12 сцен");
    CHECK(scenery_dynamic(10) && scenery_hidden(11) && !scenery_hidden(10), "пары Day/Night распознаются");
    wm_set_time(12, 0, 15, 11, 2026);
    settings_set_wallpaper(WALL_COUNT + 10); ticks(10); shot("w_day");
    wm_set_time(22, 0, 15, 11, 2026); ticks(10); shot("w_night");
    CHECK(settings_wallpaper() == WALL_COUNT + 10, "выбор остался на дневном id");
    open_window(APP_SETTINGS); ticks(60); wins[APP_SETTINGS].x = 150; wins[APP_SETTINGS].y = 40; ticks(40); shot("w_page");
    settings_set_wallpaper(WALL_COUNT + 8); wm_set_time(12, 0, 15, 11, 2026); ticks(10); shot("w_petal_day");
    printf("fails=%d\n", fails);
    return fails;
}
