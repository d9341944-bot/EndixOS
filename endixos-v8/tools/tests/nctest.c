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
    notify_icon("Software Update", "EndixOS 8.2 is available", -1);
    notify_icon("Timer", "Time's up", ICON_APP_CLOCK);
    notify_icon("Trash", "Moved 2 items to Trash", ICON_TRASH);
    ticks(30);
    CHECK(nh_n == 3 && !nc_open, "история: 3 уведомления, центр закрыт");
    cl(1024 - 40, 10); ticks(5);
    CHECK(nc_open, "клик по часам открывает центр уведомлений");
    shot("nc");
    cl(nc_x() + 100, nc_y() + NC_HEAD + 4 + NC_ROW + 10); CHECK(nh_n == 2 && nc_open, "клик по карточке убирает её");
    for (int k = 0; k < 20; k++) notify_icon("Test", "Another notification", -1);
    CHECK(nh_n == 22, "история копится");
    key(0, 0); nc_wheel(-1); CHECK(nc_scroll == 1, "колесо прокручивает список");
    cl(nc_x() + NC_W - 50, nc_y() + 28); CHECK(nh_n == 0, "Clear очищает");
    shot("nc_empty");
    key(27, 0); CHECK(!nc_open, "Esc закрывает");
    cl(1024 - 40, 10); cl(300, 300); CHECK(!nc_open, "клик мимо закрывает");
    printf("fails=%d\n", fails);
    return fails;
}
