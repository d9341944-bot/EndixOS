#include "doomapp.h"
#include "fb.h"
#include "font.h"
#include "theme.h"
#include "../drivers/pit.h"
#include "../drivers/ps2.h"
#include "../drivers/usb.h"
#include "../kernel/blk.h"
#include "../kernel/event.h"
#include "../doom/doom_plat.h"
#include "../doom/doom_sys.h"
#include "photos.h"
#include "../drivers/hda.h"

enum { S_IDLE, S_LOADING, S_RUN, S_ERR, S_DONE };
static int st = S_IDLE, was_active;
static char msg[96], wadname[40];
static u32 big[DOOMAPP_W * DOOMAPP_H];

uint32_t doom_now_ms(void) { return pit_ticks() * (1000u / PIT_HZ); }
void doom_idle(void) { __asm__ volatile("sti; hlt"); }
extern void serial_putc(char c) __attribute__((weak));
void doom_serial(char c) { if (serial_putc) serial_putc(c); }

/* ---- поиск и чтение WAD ---- */
typedef struct { char alias[16]; u32 size; int found; } hit_t;
static int is_wad(const char *n) { int l = 0; while (n[l]) l++; return l > 4 && n[l - 4] == '.' && (n[l - 3] | 32) == 'w' && (n[l - 2] | 32) == 'a' && (n[l - 1] | 32) == 'd'; }
static int on_entry(void *ctx, const char *name, const char *alias, u32 size, int is_dir)
{
    hit_t *h = ctx;
    if (is_dir || h->found || !is_wad(name) || size < 100000) return 0;
    int i = 0; for (; alias[i] && i < 15; i++) h->alias[i] = alias[i];
    h->alias[i] = 0; h->size = size; h->found = 1;
    for (i = 0; i < 39 && name[i]; i++) wadname[i] = name[i];
    wadname[i] = 0;
    return 1;
}
static const void *load_from(fat_t *f, u32 *len)
{
    static const char *const dirs[] = { "/", "/Games", "/GAMES", "/Doom", "/DOOM", "/WAD", "/Pictures" };
    for (unsigned d = 0; d < sizeof dirs / sizeof *dirs; d++) {
        hit_t h; h.found = 0; h.size = 0;
        fat_list(f, dirs[d], on_entry, &h);
        if (!h.found) continue;
        if (h.size > doom_wad_max()) { msg[0] = 0; continue; }
        char path[48]; int k = 0;
        for (const char *p = dirs[d]; *p; p++) path[k++] = *p;
        if (k > 1) path[k++] = '/';
        for (int i = 0; h.alias[i]; i++) path[k++] = h.alias[i];
        path[k] = 0;
        void *buf = doom_alloc_wad(h.size);
        if (!buf) return 0;
        int n = fat_read(f, path, buf, h.size);
        if (n != (int)h.size) return 0;
        *len = h.size; return buf;
    }
    return 0;
}
static const void *find_wad(u32 *len)
{
    static fat_t f;
    const void *w = 0;
    if (photos_volume(&f) && (w = load_from(&f, len))) return w;
    if (usb_drive_count() == 0) usb_scan();
    for (int i = 0; i < usb_drive_count(); i++) {
        blk_t *b = usb_blk(i); u8 type; u32 start, size;
        if (mbr_get_part(b, 0, &type, &start, &size) != 0) start = 0;
        if (fat_mount(b, start, &f) == 0 && (w = load_from(&f, len))) return w;
    }
    return 0;
}

static void set(const char *s) { int i = 0; for (; s[i] && i < 95; i++) msg[i] = s[i]; msg[i] = 0; }

static int raw(u8 sc, int ext, int press) { doom_input(sc, ext, press); return 1; }
int doom_audio_start(const int16_t *ring, uint32_t frames) { return hda_present() && hda_play_loop(ring, frames); }
uint32_t doom_audio_pos(void) { return hda_pos_frames(); }
void doom_audio_stop(void) { hda_stop(); }
static void grab(int on) { ps2_raw_hook = on ? raw : 0; }

static void start(void)
{
    u32 len = 0;
    const void *w = find_wad(&len);
    if (!w) { set("No WAD found. Put DOOM1.WAD in /Games on the disk or a USB stick (FAT32)."); st = S_ERR; return; }
    if (doom_start(w, len, wadname) != 0) { set(doom_error()[0] ? doom_error() : "Doom failed to start"); st = S_ERR; return; }
    st = S_RUN;
}

void doomapp_init(void) { }
int doomapp_click(void) { if (st == S_IDLE || st == S_ERR) { st = S_LOADING; return 1; } return 0; }
int doomapp_key(i32 key) { if (key == '\n' || key == '\r' || key == ' ') return doomapp_click(); return 0; }

int doomapp_tick(u32 ticks, int active)
{
    (void)ticks;
    if (st == S_LOADING) { if (active) { start(); return 1; } return 0; }
    if (st != S_RUN) { if (was_active) { grab(0); was_active = 0; } return 0; }
    if (!doom_alive()) { grab(0); doom_snd_stop(); st = S_DONE; was_active = 0; set("The game has ended. Restart EndixOS to play again."); return 1; }
    if (active != was_active) {
        if (!active) { doom_release_all(); grab(0); doom_snd_stop(); } else { grab(1); doom_snd_resume(); }
        was_active = active;
    }
    if (!active) return 0;
    doom_frame();
    return 1;
}

void doomapp_draw(i32 x, i32 y, i32 w, i32 h)
{
    if (st == S_RUN || (st == S_LOADING && 0)) {
        const u32 *s = doom_screen();
        if (s) {
            for (int j = 0; j < 200; j++) {
                u32 *r0 = big + (j * 2) * 640, *r1 = r0 + 640; const u32 *sr = s + j * 320;
                for (int i = 0; i < 320; i++) { u32 p = sr[i]; r0[i * 2] = r0[i * 2 + 1] = r1[i * 2] = r1[i * 2 + 1] = p; }
            }
            fb_blit(x + (w - 640) / 2, y + (h - 400) / 2, big, 640, 400);
        }
        return;
    }
    fb_fill_rect(x, y, w, h, RGB(18, 8, 8));
    const char *t = "DOOM";
    i32 tw = font_width(t, FONT_BOLD);
    font_draw(x + (w - tw) / 2, y + h / 2 - 60, t, RGB(220, 40, 30), FONT_BOLD);
    const char *l1 = st == S_LOADING ? "Loading WAD..." : st == S_ERR || st == S_DONE ? msg : "Click or press Enter to start";
    font_draw(x + (w - font_width(l1, 0)) / 2, y + h / 2 - 20, l1, RGB(230, 230, 230), 0);
    if (st == S_IDLE || st == S_ERR) {
        const char *l2 = "Original id Software engine. Needs DOOM1.WAD (not included).";
        font_draw(x + (w - font_width(l2, 0)) / 2, y + h / 2 + 14, l2, RGB(150, 150, 150), 0);
    }
}
