#include "wav.h"
#include "gui/clockapp.c"
#include "gui/calapp.c"
#include "gui/music.c"

#include "gui/wm.c"
#include "gui/timeset.h"
#include "gui/settings.h"
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
    u32 y, mo, d, h, mi, sc;
    CHECK(ts_from_http_date("Sun, 15 Nov 2026 11:55:01 GMT", 420, &y, &mo, &d, &h, &mi, &sc) == 0 && y == 2026 && mo == 11 && d == 15 && h == 18 && mi == 55 && sc == 1, "Date + UTC+7 даёт местное время");
    CHECK(ts_from_http_date("Mon, 01 Jan 2029 02:00:00 GMT", -300, &y, &mo, &d, &h, &mi, &sc) == 0 && y == 2028 && mo == 12 && d == 31 && h == 21, "переход назад через год");
    CHECK(ts_from_http_date("Wed, 28 Feb 2024 23:30:00 GMT", 60, &y, &mo, &d, &h, &mi, &sc) == 0 && mo == 2 && d == 29 && h == 0 && mi == 30, "високосный год");
    CHECK(ts_from_http_date("Sun, 31 Dec 2028 23:30:00 GMT", 840, &y, &mo, &d, &h, &mi, &sc) == 0 && y == 2029 && mo == 1 && d == 1 && h == 13, "переход вперёд через год");
    CHECK(ts_from_http_date("garbage", 0, &y, &mo, &d, &h, &mi, &sc) != 0 && ts_from_http_date("", 0, &y, &mo, &d, &h, &mi, &sc) != 0, "мусор отвергается");
    CHECK(ts_apply(2026, 11, 31, 10, 0) != 0 && ts_apply(2026, 11, 15, 24, 0) != 0 && ts_apply(2019, 1, 1, 0, 0) != 0, "неверные значения отвергаются");
    CHECK(ts_apply(2026, 11, 15, 18, 55) == 0, "ручная установка принята");
    ts_now(&h, &mi, &d, &mo, &y); CHECK(h == 18 && mi == 55 && d == 15 && mo == 11 && y == 2026, "время обновилось");
    CHECK(settings_click(15, 94 + 9 * 30 + 5), "страница Date & Time открывается из списка");
    for (int k = 0; k < 14; k++) settings_click(194 + 422 - 16 - 26 + 5, 84 + 42 + 12);     /* пояс +7 часов */
    CHECK(ts_tz() == 420, "кнопка + двигает пояс на 30 минут");
    settings_click(194 + 422 - 52 + 10, 84 + 84 + 15); CHECK(ts_auto() == 1, "переключатель автосинхронизации");
    settings_click(194 + 10 + 106 + 5, 84 + 3 * 42 + 66 + 9 + 5);    /* день +1 */
    settings_click(194 + 10 + 2 * 138 + 106 + 5, 84 + 3 * 42 + 66 + 9 + 5);    /* год +1 */
    settings_click(194 + 10 + 138 + 6 + 100, 84 + 3 * 42 + 66 + 42 + 9 + 5);    /* Set - не должно упасть */
    ticks(10); wm_set_time(18, 55, 15, 11, 2026); open_window(APP_SETTINGS); ticks(60); wins[APP_SETTINGS].x = 150; wins[APP_SETTINGS].y = 30; ticks(30);
    settings_click(15, 94 + 9 * 30 + 5); ticks(30); shot("dt");
    printf("fails=%d\n", fails);
    return fails;
}
