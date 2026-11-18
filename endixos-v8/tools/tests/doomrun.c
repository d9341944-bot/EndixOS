/* Хост-тест Doom: запускает настоящий движок на WAD-файле без libc, пишет кадры в PPM.
 * Сборка: tools/tests/doomrun.sh <wad> */
#include <stdint.h>
#include "../../doom/doom_plat.h"
static void num_(int v);
static int sys3(int n, int a, int b, int c) { int r; __asm__ volatile("int $0x80" : "=a"(r) : "0"(n), "b"(a), "c"(b), "d"(c) : "memory"); return r; }
static void wr(int fd, const char *s, int n) { sys3(4, fd, (int)s, n); }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
void doom_file_written(const char *n, const void *d, uint32_t s) { (void)d; wr(1, "file written: ", 14); wr(1, n, slen(n)); wr(1, " ", 1); num_(s); wr(1, "\n", 1); }
void doom_serial(char c) { wr(2, &c, 1); }
static uint32_t fake;
uint32_t doom_now_ms(void) { return fake += 1; }
void doom_idle(void) { fake += 4; }
static const int16_t *aring; static uint32_t aframes, at0, alast; static int16_t acap[48000 * 20 * 2]; static uint32_t ncap;
int doom_audio_start(const int16_t *r, uint32_t f) { aring = r; aframes = f; at0 = fake; alast = 0; return 1; }
uint32_t doom_audio_pos(void) {
    if (!aring) return 0;
    uint32_t p = ((fake - at0) * 48u) % aframes;
    while (alast != p) { if (ncap < sizeof acap / 4) { acap[ncap * 2] = aring[alast * 2]; acap[ncap * 2 + 1] = aring[alast * 2 + 1]; ncap++; } alast = (alast + 1) % aframes; }
    return p;
}
void doom_audio_stop(void) { aring = 0; }
static void num(int v);
static void num_(int v) { num(v); }
static void ppm(const char *name)
{
    int fd = sys3(5, (int)name, 0101 | 01000, 0644);
    wr(fd, "P6\n320 200\n255\n", 15);
    static uint8_t row[320 * 3]; const uint32_t *s = doom_screen();
    for (int y = 0; y < 200; y++) { for (int x = 0; x < 320; x++) { uint32_t p = s[y * 320 + x]; row[x*3] = p >> 16; row[x*3+1] = p >> 8; row[x*3+2] = p; } wr(fd, (char *)row, sizeof row); }
    sys3(6, fd, 0, 0);
}
static void num(int v) { char b[12]; int k = 0; if (!v) b[k++] = '0'; while (v) { b[11 - k++] = '0' + v % 10; v /= 10; } char o[12]; for (int i = 0; i < k; i++) o[i] = b[12 - k + i]; wr(1, o, k); }
void _start_c(int *sp)
{
    char **argv = (char **)(sp + 1);
    int fd = sys3(5, (int)argv[1], 0, 0);
    if (fd < 0) { wr(1, "no wad\n", 7); sys3(1, 1, 0, 0); }
    uint32_t cap = 36u << 20; uint8_t *w = doom_alloc_wad(cap); int n = 0, r;
    while ((r = sys3(3, fd, (int)w + n, 1 << 20)) > 0) n += r;
    wr(1, "wad bytes: ", 11); num(n); wr(1, "\n", 1);
    if (doom_start(w, n, "doom.wad")) { wr(1, "start failed: ", 14); wr(1, doom_error(), slen(doom_error())); wr(1, "\n", 1); sys3(1, 1, 0, 0); }
    wr(1, "started\n", 8);
    int shots = 0;
    for (int f = 0; f < 700 && doom_alive(); f++) {
        if (f == 100 || f == 160) { doom_input(0x1C, 0, 1); doom_frame(); doom_input(0x1C, 0, 0); }   /* Enter: меню / новая игра */
        if (f == 500) { doom_input(0x3C, 0, 1); doom_frame(); doom_input(0x3C, 0, 0); }
        if (f == 505 || f == 515 || f == 525) { int k = f == 505 ? 0x1C : f == 515 ? 0x1E : 0x1C; doom_input(k, 0, 1); doom_frame(); doom_input(k, 0, 0); }
        if (f > 330 && f < 380) doom_mouse(f % 2 ? 4 : -4, 0, f == 340);
        if (f == 210) doom_input(0x1D, 0, 1);                                                          /* Ctrl удержан: стрельба */
        if (f == 215) doom_input(0x48, 1, 1);                                                          /* вперёд */
        doom_frame();
        if (f == 508 || f == 518 || f == 540 || f == 690) { char nm[] = "/tmp/doom_0.ppm"; nm[10] = '0' + shots++; ppm(nm); }
    }
    { int pk = 0; uint32_t nz = 0; for (uint32_t i = 0; i < ncap * 2; i++) { int v = acap[i] < 0 ? -acap[i] : acap[i]; if (v > pk) pk = v; if (v) nz++; }
      wr(1, "audio frames captured: ", 23); num(ncap); wr(1, " nonzero samples: ", 18); num(nz); wr(1, " peak: ", 7); num(pk); wr(1, "\n", 1);
      int fd = sys3(5, (int)"/tmp/doom.raw", 0101 | 01000, 0644); wr(fd, (char *)acap, ncap * 4); sys3(6, fd, 0, 0); }
    wr(1, "done, alive=", 12); num(doom_alive()); wr(1, "\n", 1);
    sys3(1, 0, 0, 0);
}
__asm__(".globl _start\n_start:\n movl %esp, %eax\n andl $-16, %esp\n pushl %eax\n pushl %eax\n call _start_c\n");
