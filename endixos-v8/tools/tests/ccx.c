#include "gui/wm.c"
#include "gui/control.c"
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
static void click(i32 x, i32 y) { mouse(x, y, 0); mouse(x, y, 1); mouse(x, y, 0); ticks(40); }
#define PX 700
#define TX(c) (PX + 14 + (c) * 74 + 33)
#define TY(r) (30 + 14 + (r) * 74 + 33)
static i32 foot(void) { return 30 + cur_h() - 14 - 32 + 2 + 15; }
int main(int argc, char **argv)
{
    prefix = argv[1];
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    if (fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32)) return 2;
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); wm_set_time(16, 35, 2, 11, 2026); ticks(30);
    click(cc_icon_x() + 8, 8); ticks(60);
    CHECK(cc_is_open(), "Control Center открыт");
    frame("c0");
    CHECK(wm_get_option(WM_OPT_REDUCE_TRANSP) == 0, "Reduce Transparency выключен");
    click(TX(0), TY(0)); CHECK(!wifi, "Wi-Fi переключается");
    click(TX(2), TY(1)); ticks(5); CHECK(theme_is_dark(), "Dark Mode включает тёмную тему");
    click(TX(2), TY(1)); CHECK(!theme_is_dark(), "и выключает");
    int a0 = theme_accent_index(); click(TX(0), TY(2)); CHECK(theme_accent_index() == (a0 + 1) % ACCENT_COUNT, "Accent меняет цвет");
    { i32 sy = TY(3); mouse(PX + 14 + 150, sy, 0); mouse(PX + 14 + 150, sy, 1); CHECK(bright > 20 && bright < 100, "ползунок Display: нажатие ставит значение"); mouse(PX + 14 + 250, sy, 1); mouse(PX + 14 + 250, sy, 0); CHECK(bright > 80, "ползунок тянется"); }
    click(TX(2), TY(2)); ticks(5); CHECK(!cc_is_open() && sp_on, "Spotlight-кнопка закрывает панель и открывает Spotlight"); sp_close();
    click(cc_icon_x() + 8, 8); ticks(60);
    /* редактор */
    i32 fy = foot(), fx = PX + 316 / 2;
    click(fx, fy); CHECK(cc_editing(), "Edit Controls включает редактор"); ticks(30); frame("c1_edit");
    u8 before[16]; cc_get_layout(before);
    int n0 = norder;
    click(PX + 14 + 4, 30 + 14 + 2 * 74 + 4);        /* минус на Accent (0,2) */
    CHECK(norder == n0 - 1 && !enabled(C_ACCENT), "минус убирает плитку");
    ticks(40); frame("c2_removed");
    /* перестановка: тянем Bluetooth (0,1) на Focus (2,0) */
    int idx_bt = 0; for (int i = 0; i < norder; i++) if (order[i] == C_BT) idx_bt = i;
    mouse(TX(0), TY(1), 0); mouse(TX(0), TY(1), 1);
    for (int k = 1; k <= 6; k++) mouse(TX(0) + (TX(2) - TX(0)) * k / 6, TY(1) + (TY(0) - TY(1)) * k / 6, 1);
    frame("c3_drag");
    mouse(TX(2), TY(0), 0); ticks(60);
    int idx_bt2 = 0; for (int i = 0; i < norder; i++) if (order[i] == C_BT) idx_bt2 = i;
    CHECK(idx_bt2 < idx_bt, "перетаскивание меняет порядок");
    frame("c4_dropped");
    /* галерея */
    fy = foot();
    click(PX + 14 + 70, fy); CHECK(mode == 2, "Add a Control открывает галерею"); ticks(40); frame("c5_gallery");
    int nn = norder;
    click(PX + 14 + 70, 30 + 14 + 42 + 22);          /* первый в списке: Accent */
    CHECK(norder == nn + 1 && mode == 1, "выбор в галерее добавляет плитку и возвращает в редактор");
    ticks(40);
    u8 lay[16]; cc_get_layout(lay);
    cc_close(); mode = 0;
    cc_set_layout(before); u8 chk[16]; cc_get_layout(chk);
    int same = 1; for (int i = 0; i < 16; i++) if (chk[i] != before[i]) same = 0;
    CHECK(same, "сохранённый состав восстанавливается");
    cc_set_layout(lay);
    cc_toggle(); ticks(60); mode = 1; ticks(5); CHECK(cc_escape() && mode == 0 && cc_is_open(), "Esc выходит из редактора, панель остаётся");
    frame("c6_final");
    printf("fails=%d\n", fails); return fails;
}
