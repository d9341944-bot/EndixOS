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
    ECM[0x10] = 73; ECM[0x20] = 1;
    CHECK(!bat_configured() && bat_width() == 0, "no battery before setup");
    bat_config(0x10, 0x20, 1); CHECK(bat_poll(1000), "EC read");
    CHECK(bat_percent() == 73 && bat_ac() == 1, "73 percent, AC");
    ticks(10); shot("bat_ac");
    ECM[0x20] = 0; ECM[0x10] = 12; bat_poll(2000); CHECK(bat_percent() == 12 && bat_ac() == 0, "12 percent, battery");
    ticks(10); shot("bat_low");
    ECM[0x10] = 200; bat_poll(3000); CHECK(bat_percent() == -1, "out of range dropped");
    ECM[0x10] = 100; bat_poll(4000); CHECK(bat_percent() == 100, "100 percent");
    bat_config(-1, -1, 1); CHECK(!bat_configured() && bat_width() == 0, "off hides icon");
    /* автоопределение: RC=2500, FCC=4000 (слова) на 0x40, напряжение 7600 на 0x46, шум вокруг */
    for (int i = 0; i < 256; i++) ECM[i] = (u8)(i % 3);
    ECM[0x40] = 2500 & 255; ECM[0x41] = 2500 >> 8; ECM[0x42] = 4000 & 255; ECM[0x43] = 4000 >> 8; ECM[0x46] = 7600 & 255; ECM[0x47] = 7600 >> 8;
    extern int bat_autodetect_info(int *); extern int bat_autodetect(void);
    CHECK(bat_autodetect() == 1, "автоопределение нашло блок данных");
    int cnt; CHECK(bat_autodetect_info(&cnt) == 0x40 && cnt == 1, "смещение 0x40, один кандидат");
    bat_poll(10000); CHECK(bat_percent() == 62, "2500/4000 = 62 процента");
    ECM[0x40] = 2600 & 255; ECM[0x41] = 2600 >> 8; bat_poll(20000); CHECK(bat_percent() == 65 && bat_ac() == 1, "заряд растёт - адаптер");
    ECM[0x40] = 2000 & 255; ECM[0x41] = 2000 >> 8; bat_poll(30000); CHECK(bat_percent() == 50 && bat_ac() == 0, "заряд падает - от батареи");
    for (int i = 0; i < 256; i++) ECM[i] = 0;
    CHECK(bat_autodetect() == 0 && !bat_configured(), "пустой EC: ничего не показываем");
    for (int i = 0; i < 256; i += 4) { ECM[i] = 0xA0; ECM[i + 1] = 0x0F; ECM[i + 2] = 0xA0; ECM[i + 3] = 0x0F; }
    CHECK(bat_autodetect() == 0, "неоднозначный EC (много кандидатов): ничего не показываем");
    printf("fails=%d\n", fails);
    return fails;
}
