/* Платформа doomgeneric для EndixOS: арена, очередь клавиш, экран. */
#include <string.h>
#include <stdlib.h>
#include "doomgeneric.h"
#include "doomkeys.h"
#include "doom_sys.h"
#include "doom_plat.h"

#ifndef DOOM_ARENA_MB
#define DOOM_ARENA_MB 40
#endif
#define ARENA ((uint32_t)DOOM_ARENA_MB << 20)
static uint8_t arena[ARENA] __attribute__((aligned(16)));
static int alive, started;
static char err[96], logl[96]; static int logn;
extern void doomgeneric_Create(int argc, char **argv);
extern void doomgeneric_Tick(void);

__attribute__((weak)) void doom_serial(char c) { (void)c; }
void doom_putc(char c)
{
    doom_serial(c);
    if (c == '\n') { if (logn) { logl[logn] = 0; strncpy(err, logl, 95); } logn = 0; }
    else if (logn < 95) logl[logn++] = c;
}
void doom_exit_hook(int code) { (void)code; alive = 0; }

/* ---- клавиши ---- */
static uint16_t q[64]; static int qh, qt; static uint8_t held[256];
static const uint8_t sc_ascii[0x3A] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', KEY_BACKSPACE, 9,
    'q','w','e','r','t','y','u','i','o','p','[',']', 13, KEY_RCTRL, 'a','s',
    'd','f','g','h','j','k','l',';','\'','`', KEY_RSHIFT,'\\','z','x','c','v',
    'b','n','m',',','.','/', KEY_RSHIFT, '*', KEY_RALT, ' ' };
static int map_key(int sc, int ext)
{
    if (ext) switch (sc) {
        case 0x48: return KEY_UPARROW; case 0x50: return KEY_DOWNARROW; case 0x4B: return KEY_LEFTARROW; case 0x4D: return KEY_RIGHTARROW;
        case 0x1D: return KEY_RCTRL; case 0x38: return KEY_RALT; case 0x1C: return 13; case 0x53: return KEY_DEL; case 0x47: return KEY_HOME;
        case 0x4F: return KEY_END; case 0x49: return KEY_PGUP; case 0x51: return KEY_PGDN; case 0x52: return KEY_INS; case 0x35: return '/';
        default: return 0; }
    if (sc < 0x3A) return sc_ascii[sc];
    if ((sc >= 0x3B && sc <= 0x44) || sc == 0x57 || sc == 0x58) return 0x80 + sc;
    return 0;
}
void doom_input(int sc, int ext, int press)
{
    int k = map_key(sc, ext); if (!k) return;
    int idx = ext ? 128 + (sc & 127) : (sc & 127);
    if (press == held[idx] && press) return;                 /* автоповтор не нужен: движок сам знает про зажатое */
    held[idx] = (uint8_t)press;
    int n = (qt + 1) & 63; if (n == qh) return;
    q[qt] = (uint16_t)((press ? 0x100 : 0) | k); qt = n;
}
void doom_release_all(void)
{
    for (int i = 0; i < 256; i++) if (held[i]) { held[i] = 0; int k = map_key(i & 127, i >= 128); if (k) { int n = (qt + 1) & 63; if (n != qh) { q[qt] = (uint16_t)k; qt = n; } } }
}
int DG_GetKey(int *pressed, unsigned char *key)
{
    if (qh == qt) return 0;
    *pressed = (q[qh] >> 8) & 1; *key = (unsigned char)q[qh]; qh = (qh + 1) & 63; return 1;
}

void DG_Init(void) { }
void DG_SleepMs(uint32_t ms) { (void)ms; doom_idle(); }
uint32_t DG_GetTicksMs(void) { return doom_now_ms(); }
void DG_SetWindowTitle(const char *t) { (void)t; }
void DG_DrawFrame(void) { }

void *doom_alloc_wad(uint32_t size) { if (!started) { doom_mem_init(arena, ARENA); started = 1; } return malloc(size); }

int doom_start(const void *wad, uint32_t wadsize, const char *wadname)
{
    static char *argv[] = { "doom", "-iwad", "doom.wad", "-nomusic", 0 };
    if (!started) { doom_mem_init(arena, ARENA); started = 1; }
    err[0] = 0; qh = qt = 0; memset(held, 0, sizeof held);
    doom_files_reset();
    doom_file_add("doom.wad", wad, wadsize); (void)wadname;
    alive = 1;
    if (doom_setjmp()) { alive = 0; if (!err[0]) strcpy(err, "Doom stopped"); return -1; }
    doomgeneric_Create(4, argv);
    return 0;
}
void doom_frame(void)
{
    if (!alive) return;
    if (doom_setjmp()) { alive = 0; return; }
    doomgeneric_Tick();
    doom_snd_pump();
}
int doom_alive(void) { return alive; }
const uint32_t *doom_screen(void) { return (const uint32_t *)DG_ScreenBuffer; }
const char *doom_error(void) { return err; }
void doom_stop(void) { alive = 0; doom_snd_stop(); }
uint32_t doom_wad_max(void) { return ARENA - (10u << 20); }     /* зона 6 МБ и кадровые буферы остаются в арене */
