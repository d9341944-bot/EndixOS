
extern int printf(const char *, ...);
#include "kernel/blk.h"
#include "gui/theme.h"
static blk_t b; static fat_t gf;
#include "gui/prefs.h"
#define res_mount_unused
static u8 disk[200000 * 512];
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static int d_rd(void *c, u32 l, u32 n, void *p) { (void)c; for (u32 i = 0; i < n * 512; i++) ((u8 *)p)[i] = disk[(u64)l * 512 + i]; return 0; }
static int d_wr(void *c, u32 l, u32 n, const void *p) { (void)c; for (u32 i = 0; i < n * 512; i++) disk[(u64)l * 512 + i] = ((const u8 *)p)[i]; return 0; }
fat_t *res_mount(void) { return fat_mount(&b, 2048, &gf) == 0 ? &gf : 0; }
static int wall, dark, acc, perf, wopt[8], gl[16], wmask, noupd, wpos_[8];
static char ss[33], pp[64]; static int won;
int settings_wallpaper(void) { return wall; } void settings_set_wallpaper(int i) { wall = i; }
int theme_is_dark(void) { return dark; } void theme_set_dark(int d) { dark = d; }
int theme_accent_index(void) { return acc; } void theme_set_accent(int a) { acc = a; }
int wm_get_option(int i) { return wopt[i]; } void wm_set_option(int i, int v) { wopt[i] = v; }
int fb_glass_param_get(int i) { return gl[i]; } void fb_glass_param_set(int i, int v) { gl[i] = v; }
int widgets_get_mask(void) { return wmask; } void widgets_set_mask(int m) { wmask = m; }
void widgets_get_pos(u8 *o) { (void)o; } void widgets_set_pos(const u8 *o) { (void)o; }
void cc_get_layout(u8 *o) { (void)o; } void cc_set_layout(const u8 *o) { (void)o; }
int update_auto(void) { return !noupd; } void update_set_auto(int a) { noupd = !a; }
static int istyle; int icon_style(void) { return istyle; } void icon_set_style(int v) { istyle = v; }
static int av_, avc_; int avatar_get(void) { return av_; } void avatar_set(int a) { av_ = a; } int avatar_color(void) { return avc_; } void avatar_set_color(int c) { avc_ = c; }
int perf_mode(void) { return perf; } void perf_set_mode(int m) { perf = m; }
void wifi_pref_get(char *s, char *p, int *on) { for (int i = 0; i < 33; i++) s[i] = ss[i]; for (int i = 0; i < 64; i++) p[i] = pp[i]; *on = won; }
void wifi_pref_set(const char *s, const char *p, int on) { for (int i = 0; i < 33; i++) ss[i] = s[i]; for (int i = 0; i < 64; i++) pp[i] = p[i]; won = on; }
int main(void)
{
    b.rd = d_rd; b.wr = d_wr; b.ctx = 0; b.sectors = 200000;
    mbr_set_single(&b, 2048, 190000, 0x0C); fat_format(&b, 2048, 190000, "T", &gf);
    prefs_init(0, 0);
    CHECK(1, "старт без диска");
    fat_mkdir(&gf, "/boot"); fat_mkdir(&gf, "/boot/grub"); fat_write(&gf, "/boot/grub/grub.cfg", "x", 1);
    prefs_init(0, 0);   /* res_mount в тесте не проверяет grub.cfg */
    CHECK(1, "файловое хранилище");
    wall = 3; dark = 1; acc = 2; perf = 1; istyle = 3; av_ = 7; avc_ = 4; wopt[1] = 1; gl[2] = 77; won = 1; ss[0]='H'; ss[1]='i'; pp[0]='p'; pp[1]='w';
    for (u32 t = 100; t < 600; t += 50) prefs_tick(t);
    wall = dark = acc = perf = istyle = av_ = avc_ = 0; wopt[1] = 0; gl[2] = 0; won = 0; ss[0] = pp[0] = 0;
    prefs_init(0, 0);
    CHECK(wall == 3 && dark == 1 && acc == 2 && perf == 1 && istyle == 3 && av_ == 7 && avc_ == 4 && wopt[1] == 1 && gl[2] == 77, "обои/тема/настройки вернулись после перезагрузки");
    CHECK(won == 1 && ss[0] == 'H' && ss[1] == 'i' && pp[0] == 'p', "Wi-Fi: включён, сеть и пароль вернулись");
    printf("fails=%d\n", fails); return fails;
}
