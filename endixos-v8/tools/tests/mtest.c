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
static int fails;
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
    int md = fs_lookup(fs_home(), "Music"); if (md < 0) md = fs_mkdir(fs_home(), "Music"); int w = fs_create(md, "beep.wav"); fs_write(w, (const char *)WAV, sizeof WAV, 0);
    open_window(APP_MUSIC); ticks(40);
    CHECK(ntr == 9, "3 встроенных + 1 WAV из ~/Music");
    i32 mx = wins[APP_MUSIC].x, my = wins[APP_MUSIC].y + TITLE_H;
    cl(mx + 100, my + LISTY + 4); ticks(50);
    CHECK(cur == 0 && playing && nframes > 48000 * 10, "клик по списку: Ode to Joy играет (синтез)");
    cl(mx + 200, my + 262); CHECK(!playing, "пауза");
    cl(mx + 200, my + 262); CHECK(playing, "продолжить");
    cl(mx + 200, my + BARY + 3); CHECK(off > nframes / 3, "перемотка по полосе");
    key(KEY_RIGHT, 0); CHECK(cur == 1, "следующий трек");
    play_track(8); CHECK(nframes > 23000 && nframes < 25000 && !bad, "WAV 8 кГц -> 48 кГц (0,5 с)");
    for (int k = 0; k < 8; k++) { load(k); printf("tune %d: %u frames (%.0f s)\n", k, nframes, nframes / 48000.0); if (nframes < 48000 * 5 || nframes >= MAXF) fails++; }
    play_track(5); ticks(100); shot("music");
    ticks(3000); CHECK(!playing, "WAV доиграл и остановился");
    theme_set_dark(1); ticks(30); shot("music_dark");
    printf("fails=%d\n", fails);
    return fails;
}
