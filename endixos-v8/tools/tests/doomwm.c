/* Хост-тест окна Doom: поиск WAD на FAT32 (RAM-диск), чтение кусками, запуск, кадры, перехват клавиш.
 * Движок подменён заглушкой (в 64-бит хосте он не живёт); настоящий движок проверяет doomrun.sh. */
#include "gui/wm.c"
#include "kernel/blk.h"
typedef struct _IO_FILE FILE;
extern FILE *fopen(const char *, const char *); extern unsigned fwrite(const void *, unsigned, unsigned, FILE *); extern int fclose(FILE *);
extern void *mmap(void *, unsigned long, int, int, int, long); extern int printf(const char *, ...); extern int snprintf(char *, unsigned long, const char *, ...);
extern void *malloc(unsigned long); extern void *calloc(unsigned long, unsigned long);
void sys_reboot(void) {} void sys_shutdown(void) {}
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
static u32 *vram; static u32 T = 5000;
static void ticks(int n) { for (int i = 0; i < n; i++) { T += 2; now_ticks = T; wm_tick(T); } }
static void shot(const char *n) { char p[128]; wm_render(); fb_flip(); snprintf(p, sizeof p, "dm_%s.raw", n); FILE *f = fopen(p, "wb"); fwrite(vram, 4, 1024 * 768, f); fclose(f); }

static u8 *disk; static u32 dsec;
static int d_rd(void *c, u32 l, u32 n, void *b) { (void)c; if (l + n > dsec) return -1; memcpy(b, disk + (size_t)l * 512, n * 512); return 0; }
static int d_wr(void *c, u32 l, u32 n, const void *b) { (void)c; if (l + n > dsec) return -1; memcpy(disk + (size_t)l * 512, b, n * 512); return 0; }
static blk_t B; static fat_t F;
int (*ps2_raw_hook)(u8 sc, int ext, int press);

/* заглушка движка */
#include "doom/doom_plat.h"
static u32 scr[320 * 200]; static int alive, nframes, keys; static u32 sum_seen, wadlen_seen;
void *doom_alloc_wad(uint32_t n) { return malloc(n); }
uint32_t doom_wad_max(void) { return 30u << 20; }
int doom_start(const void *w, uint32_t n, const char *nm) { (void)nm; const u8 *p = w; for (u32 i = 0; i < n; i++) sum_seen = sum_seen * 31 + p[i]; wadlen_seen = n; alive = 1; return 0; }
void doom_frame(void) { nframes++; for (int i = 0; i < 320 * 200; i++) scr[i] = (u32)((i % 320) * 255 / 320) << 16 | (u32)((i / 320) * 255 / 200) << 8 | (u32)(nframes & 255); }
int doom_alive(void) { return alive; }
const uint32_t *doom_screen(void) { return scr; }
const char *doom_error(void) { return "err"; }
void doom_stop(void) { alive = 0; }
void doom_input(int sc, int ext, int press) { (void)ext; if (press && sc == 0x1C) keys++; }
void doom_release_all(void) { keys += 100; }
void doom_snd_stop(void) {} void doom_snd_resume(void) {}
int hda_present(void) { return 1; } int hda_play_loop(const i16 *b, u32 f) { (void)b; (void)f; return 1; } u32 hda_pos_frames(void) { return 0; } void hda_stop(void) {}

int main(void)
{
    vram = mmap(0, FB_MAX_W * FB_MAX_H * 4, 3, 0x62, -1, 0);
    fb_init((u32)(unsigned long)vram, 1024 * 4, 1024, 768, 32);
    static sysinfo_t info = { "Atom", 128, 1024, 768 };
    wm_init(&info); ticks(30);
    dsec = 140000; disk = calloc(dsec, 512);
    B.rd = d_rd; B.wr = d_wr; B.sectors = dsec;
    CHECK(fat_format(&B, 0, dsec, "TEST", &F) == 0, "format");
    open_window(APP_DOOM); ticks(10); shot("idle");
    doomapp_click(); ticks(4);
    CHECK(!alive, "no WAD yet: not started");
    shot("nowad");
    static u8 wad[3 * 1000 * 1000 + 123]; u32 h = 0;
    for (u32 i = 0; i < sizeof wad; i++) wad[i] = (u8)(i * 7 + (i >> 9)), h = h * 31 + wad[i];
    memcpy(wad, "IWAD", 4); h = 0; for (u32 i = 0; i < sizeof wad; i++) h = h * 31 + wad[i];
    fat_mkdir(&F, "/Games");
    CHECK(fat_write(&F, "/Games/DOOM1.WAD", wad, sizeof wad) == 0, "WAD written");
    photos_scan_volume(&B, 0);
    doomapp_click(); ticks(4);
    CHECK(alive, "started after WAD found");
    CHECK(wadlen_seen == sizeof wad && sum_seen == h, "WAD read back byte-exact via chunked fat_read");
    ticks(10);
    CHECK(nframes > 3, "frames run");
    CHECK(ps2_raw_hook != 0, "raw key hook active while focused");
    shot("run");
    int ok = 1; u32 *fbp = vram;
    for (int y = 0; y < 20; y++) for (int x = 0; x < 1024; x++) (void)fbp[y * 1024 + x];
    CHECK(ok, "drew");
    close_window_for_test:
    ;
    printf("%s (%d)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails != 0;
}
