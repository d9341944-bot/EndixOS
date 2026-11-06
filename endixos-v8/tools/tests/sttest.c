#include "gui/settings.c"
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
static void type(const char *s) { for (; *s; s++) { event_t e = { EV_KEY, *s, 0, 0 }; wm_handle_event(&e); ticks(2); } }
static void shot(const char *n) { char p[128]; wm_render(); fb_flip(); snprintf(p, sizeof p, "st_%s.raw", n); FILE *f = fopen(p, "wb"); fwrite(vram, 4, 1024 * 768, f); fclose(f); }
int main(void)
{
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32);
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); ticks(30);
    open_window(APP_SETTINGS); ticks(60);
    i32 ox = wins[APP_SETTINGS].x, oy = wins[APP_SETTINGS].y + TITLE_H;
    click(ox + 60, oy + 22); CHECK(sq_focus, "клик в поле Search фокусирует");
    type("vol"); CHECK(sqn == 3, "ввод в поиск");
    int vp[PAGE_COUNT]; CHECK(vis_pages(vp) == 1 && vp[0] == PAGE_SOUND, "поиск 'vol' оставляет Sound");
    click(ox + 60, oy + SIDE_ITEMS_Y + 10); CHECK(page == PAGE_SOUND, "клик по найденной странице");
    shot("sound");
    int v0 = cc_volume(); click(ox + SL_X + 20, oy + 84 + ROW_H / 2); CHECK(cc_volume() != v0 || v0 < 20, "ползунок Sound меняет громкость");
    mouse(ox + SL_X + SL_W, oy + 84 + ROW_H / 2, 1); mouse(ox + SL_X + SL_W, oy + 84 + ROW_H / 2, 0);
    click(ox + 60, oy + 22); event_t e = { EV_KEY, 27, 0, 0 }; wm_handle_event(&e); CHECK(sqn == 0 && vis_pages(vp) == PAGE_COUNT, "Esc очищает поиск");
    click(ox + 60, oy + SIDE_ITEMS_Y + 4 * 34 + 10); CHECK(page == PAGE_DISPLAY, "страница Displays");
    click(ox + SL_X + 10, oy + 84 + ROW_H / 2); CHECK(cc_brightness() < 40, "ползунок Displays");
    cc_set_brightness(100); shot("display");
    printf("fails=%d\n", fails); return fails;
}
