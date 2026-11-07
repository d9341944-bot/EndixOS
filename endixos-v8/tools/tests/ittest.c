#include "gui/wm.c"
#include "gui/installer.c"
extern unsigned char blob_boot[512], blob_core[3000]; extern int atoi(const char *);
typedef struct _IO_FILE FILE;
extern FILE *fopen(const char *, const char *); extern unsigned fwrite(const void *, unsigned, unsigned, FILE *); extern int fclose(FILE *);
extern void *mmap(void *, unsigned long, int, int, int, long); extern int printf(const char *, ...); extern int snprintf(char *, unsigned long, const char *, ...);
extern FILE *istub_img; extern u32 istub_nsec; extern int istub_disks;
static int reboots; void sys_reboot(void) { reboots++; } void sys_shutdown(void) {}
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static u32 *vram; static const char *prefix; static u32 T = 5000; static int W = 1024, H = 768;
static void frame(const char *n) { char p[256]; wm_render(); fb_flip(); snprintf(p, sizeof p, "%s_%s.raw", prefix, n); FILE *f = fopen(p, "wb"); fwrite(vram, 4, (unsigned)(W * H), f); fclose(f); }
static void ticks(int n) { for (int i = 0; i < n; i++) wm_tick(T += 14); }
static void mouse(i32 x, i32 y, i32 b) { event_t e = { EV_MOUSE, x, y, b }; wm_handle_event(&e); ticks(3); }
static void click(i32 x, i32 y) { mouse(x, y, 0); mouse(x, y, 1); mouse(x, y, 0); ticks(60); }
static void key(i32 c) { event_t e = { EV_KEY, c, 0, 0 }; wm_handle_event(&e); ticks(2); }
static void type(const char *s) { for (; *s; s++) key((u8)*s); }
int main(int argc, char **argv)
{
    prefix = argv[1]; if (argc > 2) { W = atoi(argv[2]); H = atoi(argv[3]); }
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    if (fb_init((u32)(unsigned long)vram, (u32)W * 4, (u32)W, (u32)H, 32)) return 2;
    static sysinfo_t info = { "Atom", 128, 1024, 768 }; info.width = (u32)W; info.height = (u32)H;
    wm_init(&info); wm_set_time(10, 31, 24, 10, 2026);
    istub_img = fopen(argv[4], "w+b"); static unsigned char z[512]; z[450] = 7; for (u32 i = 0; i < istub_nsec; i++) fwrite(z, 512, 1, istub_img);
    for (int i = 0; i < 512; i++) blob_boot[i] = (unsigned char)(i * 3 + 1);
    for (int i = 0; i < 3000; i++) blob_core[i] = (unsigned char)(i * 5 + 2);
    installer_open(); ticks(80);
    CHECK(installer_active(), "установщик открыт"); frame("p0_welcome");
    int cw = W - 40 < 600 ? W - 40 : 600, ch = H - 20 < 440 ? H - 20 : 440, cx = (W - cw) / 2, cy = (H - ch) / 2, by = cy + ch - 52 + 16;
    click(cx + cw - 84, by); frame("p1_disk");
    CHECK(page == P_DISK, "Continue -> диск");
    click(cx + 100, cy + 28 + 56 + 20); click(cx + cw - 84, by);
    CHECK(page == P_ACCOUNT, "диск выбран -> аккаунт");
    type("Danil"); key('\t'); type("Danil_X"); frame("p2_account_bad");
    key('\t'); type("pw1"); key('\t'); type("pw2"); key('\t'); type("netbook");
    click(cx + cw - 84, by);
    CHECK(page == P_ACCOUNT && errmsg, "неверное имя/пароли -> ошибка, остаёмся"); frame("p2b_err");
    printf("err: %s\n", errmsg);
    focus = F_USER; for (int i = 0; i < 20; i++) key('\b'); type("danil");
    focus = F_PASS2; key('\b'); key('1');
    focus = F_NAME; for (int i = 0; i < 20; i++) key('\b'); type("Danil K");
    frame("p2c_ok");
    click(cx + cw - 84, by);
    CHECK(page == P_CONFIRM, "верные данные -> подтверждение");
    click(cx + cw - 84, by); CHECK(page == P_CONFIRM, "без галочки Install не работает");
    frame("p3_confirm");
    key(' '); CHECK(agree, "переключатель включён"); frame("p3b_agree");
    click(cx + cw - 84, by);
    CHECK(page == P_PROGRESS || page == P_DONE, "Install -> прогресс"); frame("p4_progress");
    ticks(10);
    CHECK(page == P_DONE, "установка завершилась"); if (page == P_ERROR) printf("ERR %s\n", errmsg);
    frame("p5_done");
    click(cx + cw - 84, by); CHECK(reboots == 1, "Restart вызывает перезагрузку");
    fclose(istub_img);
    printf("fails=%d\n", fails); return fails;
}
