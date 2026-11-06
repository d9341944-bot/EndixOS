#include "gui/wm.c"
#include "gui/login.c"
#include "kernel/userdb.h"
typedef struct _IO_FILE FILE;
extern FILE *fopen(const char *, const char *); extern unsigned fwrite(const void *, unsigned, unsigned, FILE *); extern int fclose(FILE *);
extern void *mmap(void *, unsigned long, int, int, int, long); extern int printf(const char *, ...); extern int snprintf(char *, unsigned long, const char *, ...); extern int atoi(const char *);
extern FILE *istub_img;
static int reboots; void sys_reboot(void) { reboots++; } void sys_shutdown(void) {}
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static u32 *vram; static const char *prefix; static u32 T = 5000; static int W = 1024, H = 768;
static void frame(const char *n) { char p[256]; wm_render(); fb_flip(); snprintf(p, sizeof p, "%s_%s.raw", prefix, n); FILE *f = fopen(p, "wb"); fwrite(vram, 4, (unsigned)(W * H), f); fclose(f); }
static void ticks(int n) { for (int i = 0; i < n; i++) wm_tick(T += 2); }
static void mouse(i32 x, i32 y, i32 b) { event_t e = { EV_MOUSE, x, y, b }; wm_handle_event(&e); ticks(2); }
static void key(i32 c) { event_t e = { EV_KEY, c, 0, 0 }; wm_handle_event(&e); ticks(1); }
static void type(const char *s) { for (; *s; s++) key((u8)*s); }
int main(int argc, char **argv)
{
    prefix = argv[1]; W = atoi(argv[2]); H = atoi(argv[3]);
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    if (fb_init((u32)(unsigned long)vram, (u32)W * 4, (u32)W, (u32)H, 32)) return 2;
    static sysinfo_t info = { "Atom", 128, 1024, 768 }; info.width = (u32)W; info.height = (u32)H;
    wm_init(&info); wm_set_time(16, 35, 2, 11, 2026);
    istub_img = fopen(argv[4], "r+b");
    static userdb_t u;
    CHECK(userdb_load(&u) == 0, "userdb: пользователь найден на диске");
    printf("user=%s full=%s host=%s\n", u.user, u.full, u.host);
    CHECK(u.user[0] == 'd' && u.host[0], "имя и компьютер прочитаны");
    wm_set_user(u.user, u.host);
    login_open(u.user, u.full, u.salthash); ticks(20);
    CHECK(login_active(), "экран входа открыт"); frame("l0_empty");
    type("wrong"); frame("l1_typing");
    key('\n'); ticks(5); frame("l2_shake");
    CHECK(login_active() && !login_fading(), "неверный пароль: остаёмся на экране входа");
    ticks(60);
    type("pw12"); key(0x08); 
    key('\n'); ticks(3); frame("l3_fade");
    CHECK(login_fading(), "верный пароль принят, экран гаснет");
    ticks(60); CHECK(!login_active(), "экран входа исчез"); frame("l4_desktop");
    { event_t e = { EV_KEY, KEY_LOCK, 0, 0 }; wm_handle_event(&e); ticks(2); CHECK(login_active(), "Win+L (KEY_LOCK) показывает экран входа"); login_key(27); }
    login_relock();
    CHECK(login_relock() && login_active(), "lock возвращает экран входа");
    mouse(W / 2 + 56, H - 90, 0); mouse(W / 2 + 56, H - 90, 1); mouse(W / 2 + 56, H - 90, 0);
    mouse(W / 2 - 56, H - 90, 1); CHECK(reboots == 1, "кнопка Restart работает");
    printf("fails=%d\n", fails); return fails;
}
