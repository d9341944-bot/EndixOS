#include "prefs.h"
#include "wm.h"
#include "fb.h"
#include "theme.h"
#include "settings.h"
#include "widgets.h"
#include "wallpaper.h"

#define MAGIC 0x58444E45u      /* "ENDX" */
#define PVER 1

typedef struct {
    u32 magic;
    u8  ver, wall, dark, accent, magnify, zoom, hide, clock24, reduce;
    u8  glass[GLASS_PARAMS];
    u8  widgets;
    u8  pad[512 - 4 - 9 - GLASS_PARAMS - 1];
} blob_t;

static prefs_rd_t rw_rd;
static prefs_wr_t rw_wr;
static blob_t saved, pending;
static u32 pend_t, last_chk;
static int pend_valid;

static void gather(blob_t *b)
{
    for (u32 i = 0; i < sizeof(*b); i++) ((u8 *)b)[i] = 0;
    b->magic = MAGIC; b->ver = PVER;
    b->wall = (u8)settings_wallpaper();
    b->dark = (u8)theme_is_dark();
    b->accent = (u8)theme_accent_index();
    b->magnify = (u8)wm_get_option(WM_OPT_DOCK_MAGNIFY);
    b->zoom = (u8)wm_get_option(WM_OPT_DOCK_ZOOM);
    b->hide = (u8)wm_get_option(WM_OPT_DOCK_HIDE);
    b->clock24 = (u8)wm_get_option(WM_OPT_CLOCK24);
    b->reduce = (u8)wm_get_option(WM_OPT_REDUCE_TRANSP);
    for (int i = 0; i < GLASS_PARAMS; i++) b->glass[i] = (u8)fb_glass_param_get(i);
    b->widgets = (u8)widgets_get_mask();
}

static int same(const blob_t *a, const blob_t *b)
{
    for (u32 i = 0; i < sizeof(*a); i++)
        if (((const u8 *)a)[i] != ((const u8 *)b)[i]) return 0;
    return 1;
}

static void apply(const blob_t *b)
{
    if (b->wall < WALL_COUNT) settings_set_wallpaper(b->wall);
    theme_set_dark(b->dark ? 1 : 0);
    if (b->accent < ACCENT_COUNT) theme_set_accent(b->accent);
    wm_set_option(WM_OPT_DOCK_MAGNIFY, b->magnify);
    wm_set_option(WM_OPT_DOCK_ZOOM, b->zoom);
    wm_set_option(WM_OPT_DOCK_HIDE, b->hide);
    wm_set_option(WM_OPT_CLOCK24, b->clock24);
    wm_set_option(WM_OPT_REDUCE_TRANSP, b->reduce);
    for (int i = 0; i < GLASS_PARAMS; i++) fb_glass_param_set(i, b->glass[i]);
    widgets_set_mask(b->widgets);
}

void prefs_init(prefs_rd_t rd, prefs_wr_t wr)
{
    rw_rd = rd; rw_wr = wr;
    static blob_t b;
    if (rd && rd(0, &b) == 0 && b.magic == MAGIC && b.ver == PVER)
        apply(&b);
    gather(&saved);                        /* то, что реально на экране */
}

void prefs_tick(u32 ticks)
{
    if (!rw_wr || (i32)(ticks - last_chk) < 50) return;
    last_chk = ticks;
    blob_t cur;
    gather(&cur);
    if (same(&cur, &saved)) { pend_valid = 0; return; }
    if (!pend_valid || !same(&cur, &pending)) {      /* ещё меняется: ждём, пока успокоится */
        pending = cur; pend_t = ticks; pend_valid = 1;
        return;
    }
    if ((i32)(ticks - pend_t) < 100) return;
    /* писать можно, только если сектор пустой или наш */
    static blob_t old;
    if (rw_rd && rw_rd(0, &old) == 0) {
        int ours = old.magic == MAGIC, empty = 1;
        for (u32 i = 0; i < sizeof(old) && empty; i++) if (((u8 *)&old)[i]) empty = 0;
        if (!ours && !empty) { pend_valid = 0; rw_wr = 0; return; }
    } else { rw_wr = 0; return; }
    if (rw_wr(0, &cur) == 0) saved = cur;
    pend_valid = 0;
}
