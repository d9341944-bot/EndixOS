#include "prefs.h"
#include "wm.h"
#include "fb.h"
#include "theme.h"
#include "settings.h"
#include "../drivers/ps2.h"
#include "battery.h"
#include "timeset.h"
#include "lang.h"
#include "../drivers/ps2.h"
#include "widgets.h"
#include "control.h"
#include "wallpaper.h"
#include "photos.h"
#include "update.h"
#include "perf.h"
#include "wifi.h"
#include "icons.h"
#include "avatar.h"
#include "resolution.h"
#include "../kernel/blk.h"

#define MAGIC 0x58444E45u      /* "ENDX" */
#define PVER 1

typedef struct {
    u32 magic;
    u8  ver, wall, dark, accent, magnify, zoom, hide, clock24, reduce;
    u8  glass[GLASS_PARAMS];
    u8  widgets;
    u8  wpos[8];
    u8  ccl[16];
    u8  noupd;                                   /* 1 - автопроверка обновлений выключена */
    u8  perf;                                    /* режим скорости (PERF_*) */
    u8  wifi_on;                                 /* Wi-Fi был включён */
    char ssid[33];                               /* запомненная сеть (открытым текстом) */
    char pass[64];
    u8  icon_style;                              /* стиль значков приложений (в конце, чтобы старые файлы читались) */
    u8  avatar, avatar_bg;                       /* аватарка пользователя и цвет её фона */
    u8  tp_ok, tp_speed, tp_tap, tp_nat;         /* тачпад; tp_ok = 0xA5, когда поля записаны (в старых файлах нули) */
    u8  bat_ok, bat_pct, bat_ac, bat_mask;       /* батарея: смещения в EC (bat_ok = 0xA5, когда заданы; pct 0xFF - выключено) */
    u8  ts_ok, ts_auto, ts_tz;                   /* время: ts_ok = 0xA5, когда заданы; пояс в четвертях часа + 64, автосинхронизация */
    u8  lg_ok, lg_ui, lg_kb;                     /* язык: lg_ok = 0xA5, когда заданы; язык интерфейса и раскладка при старте */
    u8  pad[512 - 3 - 3 - 4 - 9 - GLASS_PARAMS - 1 - 8 - 16 - 1 - 1 - 1 - 1 - 33 - 64 - 1 - 1 - 4 - 4];
} blob_t;

__attribute__((weak)) fat_t *res_mount(void) { return 0; }
#define CFG_PATH "/boot/endix.cfg"
static fat_t *file_fs;                                  /* установленная система: настройки лежат файлом */
_Static_assert(sizeof(blob_t) == 512, "prefs blob must be 512 bytes");
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
    widgets_get_pos(b->wpos);
    cc_get_layout(b->ccl);
    b->noupd = (u8)!update_auto();
    b->perf = (u8)perf_mode();
    b->icon_style = (u8)icon_style();
    b->avatar = (u8)avatar_get(); b->avatar_bg = (u8)avatar_color();
    { int sp, tp, nt; ps2_tp_get(&sp, &tp, &nt); b->tp_ok = 0xA5; b->tp_speed = (u8)sp; b->tp_tap = (u8)tp; b->tp_nat = (u8)nt; }
    b->ts_ok = ts_configured() ? 0xA5 : 0; b->ts_auto = (u8)ts_auto(); b->ts_tz = (u8)(ts_tz() / 15 + 64);
    b->lg_ok = 0xA5; b->lg_ui = (u8)lang_get(); b->lg_kb = (u8)kbd_layout();
    { int p, a, m; bat_get_config(&p, &a, &m); b->bat_ok = 0xA5; b->bat_pct = (u8)(p < 0 ? 0xFF : p); b->bat_ac = (u8)(a < 0 ? 0xFF : a); b->bat_mask = (u8)m; }
    int on = 0; wifi_pref_get(b->ssid, b->pass, &on); b->wifi_on = (u8)on;
}

static int same(const blob_t *a, const blob_t *b)
{
    for (u32 i = 0; i < sizeof(*a); i++)
        if (((const u8 *)a)[i] != ((const u8 *)b)[i]) return 0;
    return 1;
}

static void apply(const blob_t *b)
{
    if (b->wall < WALL_PHOTO0 + PHOTO_MAX) settings_set_wallpaper(b->wall);
    theme_set_dark(b->dark ? 1 : 0);
    if (b->accent < ACCENT_COUNT) theme_set_accent(b->accent);
    wm_set_option(WM_OPT_DOCK_MAGNIFY, b->magnify);
    wm_set_option(WM_OPT_DOCK_ZOOM, b->zoom);
    wm_set_option(WM_OPT_DOCK_HIDE, b->hide);
    wm_set_option(WM_OPT_CLOCK24, b->clock24);
    wm_set_option(WM_OPT_REDUCE_TRANSP, b->reduce);
    for (int i = 0; i < GLASS_PARAMS; i++) fb_glass_param_set(i, b->glass[i]);
    widgets_set_mask(b->widgets);
    widgets_set_pos(b->wpos);
    cc_set_layout(b->ccl);
    update_set_auto(!b->noupd);
    perf_set_mode(b->perf);
    icon_set_style(b->icon_style);
    avatar_set(b->avatar); avatar_set_color(b->avatar_bg);
    if (b->bat_ok == 0xA5) bat_config(b->bat_pct == 0xFF ? -1 : b->bat_pct, b->bat_ac == 0xFF ? -1 : b->bat_ac, b->bat_mask);
    if (b->lg_ok == 0xA5) { lang_set(b->lg_ui); kbd_set_layout(b->lg_kb); }
    if (b->ts_ok == 0xA5) ts_load((b->ts_tz - 64) * 15, b->ts_auto);
    if (b->tp_ok == 0xA5) ps2_tp_set(b->tp_speed, b->tp_tap, b->tp_nat);
    char ss[33], pp[64]; int i;
    for (i = 0; i < 32 && b->ssid[i]; i++) ss[i] = b->ssid[i]; ss[i] = 0;
    for (i = 0; i < 63 && b->pass[i]; i++) pp[i] = b->pass[i]; pp[i] = 0;
    wifi_pref_set(ss, pp, b->wifi_on ? 1 : 0);
}

void prefs_init(prefs_rd_t rd, prefs_wr_t wr)
{
    rw_rd = rd; rw_wr = wr;
    static blob_t b;
    file_fs = res_mount();                              /* на установленной системе - файл, иначе виртуальный диск */
    if (file_fs) {
        static u8 raw[512];
        if (fat_read(file_fs, CFG_PATH, raw, sizeof raw) == (int)sizeof raw) {
            for (u32 i = 0; i < sizeof b; i++) ((u8 *)&b)[i] = raw[i];
            if (b.magic == MAGIC && b.ver == PVER) apply(&b);
        }
    } else if (rd && rd(0, &b) == 0 && b.magic == MAGIC && b.ver == PVER)
        apply(&b);
    gather(&saved);                        /* то, что реально на экране */
}

void prefs_tick(u32 ticks)
{
    if (!file_fs && !rw_wr) return;
    if ((i32)(ticks - last_chk) < 50) return;
    last_chk = ticks;
    blob_t cur;
    gather(&cur);
    if (same(&cur, &saved)) { pend_valid = 0; return; }
    if (!pend_valid || !same(&cur, &pending)) {      /* ещё меняется: ждём, пока успокоится */
        pending = cur; pend_t = ticks; pend_valid = 1;
        return;
    }
    if ((i32)(ticks - pend_t) < 100) return;
    if (file_fs) {                                       /* запись файла; при ошибке пробуем позже */
        static u8 raw[512];
        for (u32 i = 0; i < sizeof cur; i++) raw[i] = ((u8 *)&cur)[i];
        if (fat_write(file_fs, CFG_PATH, raw, sizeof raw) == 0) saved = cur;
        else last_chk = ticks + 1500;                    /* не мучаем диск при сбое */
        pend_valid = 0;
        return;
    }
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
