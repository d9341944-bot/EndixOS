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
static void click(i32 x, i32 y) { mouse(x, y, 0); mouse(x, y, 1); mouse(x, y, 0); }
int main(int argc, char **argv)
{
    prefix = argv[1];
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    if (fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32)) return 2;
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); ticks(60);
    int dk = fs_resolve(fs_home(), "Desktop");
    CHECK(fs_lookup(dk, "hello.txt") < 0, "текстового документа на рабочем столе нет");
    int ins = fs_lookup(dk, "Install EndixOS");
    CHECK(ins >= 0 && desktop_is_installer(ins), "значок Install EndixOS есть");
    frame("d0");
    i32 ix = 1024 - 8 - 92 + 46, iy = 24 + 10 + 28;
    CHECK(desktop_icon_at(ix, iy) == ins, "значок найден под курсором");
    click(ix, iy); click(ix, iy); ticks(5);
    CHECK(installer_active(), "двойной клик открывает установщик");
    installer_key(27); ticks(5); CHECK(!installer_active(), "Esc закрывает установщик");
    mouse(ix, iy, 0); { event_t e = { EV_MOUSE, ix, iy, 2 }; wm_handle_event(&e); ticks(2); e.c = 0; wm_handle_event(&e); ticks(60); }
    CHECK(desktop_ctx_is_open(), "правый клик по значку открывает меню");
    frame("d1_menu");
    CHECK(desktop_ctx_node() == ins, "меню относится к значку установщика");
    click(ix + 20, iy + 8 + 26 + 6 + 8);      /* пункт Delete (второй) */
    ticks(40);
    CHECK(fs_lookup(dk, "Install EndixOS") < 0, "Delete убирает значок");
    int f = fs_create(dk, "note.txt"); (void)f; fs_mkdir(dk, "Stuff"); ticks(5);
    int st = fs_lookup(dk, "Stuff");
    i32 sx = 1024 - 8 - 92 + 46;
    ticks(80); click(sx, iy); ticks(3);
    int cnt0 = fs_count(dk);
    printf("sel=%d top=%d cnt=%d\n", desktop_selected(), top_app(), cnt0);
    { event_t e = { EV_KEY, KEY_DELETE, 0, 0 }; wm_handle_event(&e); }
    CHECK(fs_count(dk) == cnt0 - 1, "клавиша Delete убирает выбранный значок");
    (void)st; frame("d2");
    printf("fails=%d\n", fails); return fails;
}
