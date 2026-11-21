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
    CHECK(widgets_count() == 3, "по умолчанию 3 виджета");
    ticks(100); frame("d0");
    widgets_edit_start(); ticks(80); frame("e0");
    /* добавить каждый тип в каждом допустимом размере через галерею */
    int n0 = widgets_count();
    for (int k = 0; k < 8; k++) { widgets_edit_key(0x102); widgets_edit_key('\n'); ticks(10); }
    CHECK(widgets_count() > n0, "виджеты добавляются из галереи");
    ticks(60); frame("e1");
    u8 save[WIDGETS_SAVE]; widgets_save(save);
    CHECK(save[0] == 0xA5, "маркер сохранения");
    int cnt = widgets_count();
    for (int i = 0; i < 11; i++) widgets_remove(i);
    CHECK(widgets_count() == 0, "все удалены");
    widgets_load(save);
    CHECK(widgets_count() == cnt, "загрузка вернула все виджеты");
    u8 s2[WIDGETS_SAVE]; widgets_save(s2);
    int same = 1; for (int i = 0; i < WIDGETS_SAVE; i++) if (save[i] != s2[i]) same = 0;
    CHECK(same, "сохранение/загрузка раскладки совпадает");
    u8 z[WIDGETS_SAVE] = {0}; widgets_load(z);
    CHECK(widgets_count() == cnt, "пустой блок настроек игнорируется");
    CHECK(widgets_resize(0, 1) >= 0, "resize не падает");
    for (int s = 0; s < 3; s++) for (int i = 0; i < 11; i++) widgets_resize(i, s);
    widgets_edit_stop(); ticks(80); frame("e2");
    printf("fails=%d\n", fails); return fails;
}
