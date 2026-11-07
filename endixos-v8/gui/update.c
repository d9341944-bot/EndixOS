#include "update.h"
#include "settings.h"
#include "resolution.h"
#include "../kernel/net.h"
#include "../kernel/crypto.h"
#include "../kernel/blk.h"

#ifndef UPDATE_BASE
#define UPDATE_BASE "https://raw.githubusercontent.com/d9341944-bot/EndixOS/main/endixos-v8/update/"
#endif
#define UP_MAX (4u << 20)                /* максимум для ядра */
#define MAN_MAX 2048
#define TIMEOUT_MF 1500                  /* 15 с на манифест */
#define TIMEOUT_DL 6000                  /* 60 с без прогресса */

extern void sys_reboot(void);
#ifndef UPDATE_HOST
__attribute__((weak)) void wm_notify(const char *title, const char *text) { (void)title; (void)text; }
#else
void wm_notify(const char *title, const char *text);
#endif

static u8 mf[MAN_MAX];
static u8 kbuf[UP_MAX], vbuf[UP_MAX];
static int st = UP_IDLE, pct, step, auto_on = 1, autodone;
static char msg[120], ver[16], notes[480];
static int build; static u32 size; static u8 want[32];
static u32 t_start, t_last, t_progress_len, checked_at; static int checked;

void update_init(void) { st = UP_IDLE; pct = 0; msg[0] = 0; checked = 0; autodone = 0; }

static void say(const char *s) { int i = 0; while (s[i] && i < (int)sizeof msg - 1) { msg[i] = s[i]; i++; } msg[i] = 0; }
static void fail(const char *s) { say(s); st = UP_ERROR; }

int update_state(void) { return st; }
int update_percent(void) { return pct; }
const char *update_message(void) { return msg; }
const char *update_version(void) { return ver; }
int update_build(void) { return build; }
const char *update_notes(void) { return notes; }
u32 update_size(void) { return size; }
int update_can_install(void) { return res_available(); }
int update_auto(void) { return auto_on; }
void update_set_auto(int on) { auto_on = on ? 1 : 0; }
void update_restart(void) { sys_reboot(); }
int update_checked_secs(u32 now) { return checked ? (int)((now - checked_at) / 100) : -1; }

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }
static int starts(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }

int update_parse(const char *t)
{
    int hb = 0, hs = 0, hh = 0; build = 0; size = 0; ver[0] = 0; notes[0] = 0;
    while (*t) {
        const char *e = t; while (*e && *e != '\n') e++;
        char line[600]; int n = 0;
        for (const char *p = t; p < e && n < (int)sizeof line - 1; p++) if (*p != '\r') line[n++] = *p;
        line[n] = 0;
        if (starts(line, "build=")) { for (const char *p = line + 6; *p >= '0' && *p <= '9'; p++) build = build * 10 + (*p - '0'); hb = 1; }
        else if (starts(line, "size=")) { for (const char *p = line + 5; *p >= '0' && *p <= '9'; p++) size = size * 10 + (u32)(*p - '0'); hs = 1; }
        else if (starts(line, "version=")) { int i = 0; for (const char *p = line + 8; *p && i < (int)sizeof ver - 1; p++) ver[i++] = *p; ver[i] = 0; }
        else if (starts(line, "notes=")) { int i = 0; for (const char *p = line + 6; *p && i < (int)sizeof notes - 1; p++) notes[i++] = *p; notes[i] = 0; }
        else if (starts(line, "sha256=")) {
            int ok = 1; for (int i = 0; i < 32; i++) { int a = hexv(line[7 + 2 * i]), b = a < 0 ? -1 : hexv(line[8 + 2 * i]); if (a < 0 || b < 0) { ok = 0; break; } want[i] = (u8)(a << 4 | b); }
            hh = ok;
        }
        t = *e ? e + 1 : e;
    }
    return (hb && hs && hh && ver[0] && size > 0 && size < UP_MAX) ? 0 : -1;
}

static void fetch_error(void)
{
    net_fetch_t f; net_fetch_info(&f);
    fail(f.err[0] ? f.err : "Could not reach the update server.");
}

void update_check(void)
{
    if (st == UP_CHECKING || st == UP_DOWNLOADING || st == UP_INSTALLING) return;
    pct = 0; msg[0] = 0;
    if (net_fetch(UPDATE_BASE "latest.txt", mf, MAN_MAX) != 0) { fetch_error(); return; }
    st = UP_CHECKING; t_start = t_last = 0;
}

void update_install(void)
{
    if (st != UP_AVAILABLE) return;
    if (!update_can_install()) { fail("Updates can be installed only on an installed EndixOS."); return; }
    pct = 0;
    if (net_fetch(UPDATE_BASE "endixos.elf", kbuf, UP_MAX) != 0) { fetch_error(); return; }
    st = UP_DOWNLOADING; t_start = t_last = 0; t_progress_len = 0;
}

/* в grub.cfg добавляется пункт со старой версией: копия первого пункта с другим ядром */
static int cfg_add_previous(fat_t *f)
{
    static char cfg[4096];
    int n = fat_read(f, "/boot/grub/grub.cfg", cfg, sizeof cfg - 600);
    if (n <= 0) return -1;
    cfg[n] = 0;
    for (int i = 0; i + 12 < n; i++) if (starts(cfg + i, "previous.elf")) return 0;     /* уже есть */
    int a = -1;
    for (int i = 0; i + 9 < n; i++) if (starts(cfg + i, "menuentry") && (i == 0 || cfg[i - 1] == '\n')) { a = i; break; }
    if (a < 0) return -1;
    int b = -1;
    for (int i = a; i + 1 < n; i++) if (cfg[i] == '\n' && cfg[i + 1] == '}') { b = i + 2; break; }
    if (b < 0) return -1;
    int o = n; if (cfg[o - 1] != '\n') cfg[o++] = '\n';
    int q = 0;
    for (int i = a; i < b && o < (int)sizeof cfg - 40; i++) {
        if (starts(cfg + i, "\"") && !q) {                       /* заголовок пункта */
            const char *ttl = "\"EndixOS (previous version)\""; int j = 1; while (cfg[i + j] != '"' && i + j < b) j++;
            for (const char *p = ttl; *p; p++) cfg[o++] = *p;
            i += j; q = 1; continue;
        }
        if (starts(cfg + i, "/boot/endixos.elf")) { const char *r = "/boot/previous.elf"; for (; *r; r++) cfg[o++] = *r; i += 16; continue; }
        cfg[o++] = cfg[i];
    }
    return fat_write(f, "/boot/grub/grub.cfg", cfg, (u32)o);
}

static int same_hash(const u8 *d, u32 n) { u8 h[32]; sha256(d, n, h); for (int i = 0; i < 32; i++) if (h[i] != want[i]) return 0; return 1; }

static void install_step(void)
{
    fat_t *f = res_mount();
    if (!f) { fail("System disk not found."); return; }
    switch (step) {
    case 0: {                                                     /* копия текущей версии */
        int n = fat_read(f, "/boot/endixos.elf", vbuf, UP_MAX);
        if (n > 0 && fat_write(f, "/boot/previous.elf", vbuf, (u32)n) != 0) { fail("Could not back up the current version."); return; }
        pct = 93; break;
    }
    case 1:
        if (cfg_add_previous(f) != 0) { fail("Could not update the boot menu."); return; }
        pct = 96; break;
    case 2:
        if (fat_write(f, "/boot/endixos.elf", kbuf, size) != 0) { fail("Could not write the new version."); return; }
        pct = 99; break;
    case 3: {
        int n = fat_read(f, "/boot/endixos.elf", vbuf, UP_MAX);
        if (n != (int)size || !same_hash(vbuf, size)) {          /* откат: возвращаем копию */
            int m = fat_read(f, "/boot/previous.elf", vbuf, UP_MAX);
            if (m > 0) fat_write(f, "/boot/endixos.elf", vbuf, (u32)m);
            fail("The update failed verification. The previous version was restored.");
            return;
        }
        pct = 100; st = UP_READY; say("Restart to finish the update.");
        wm_notify("Software Update", "EndixOS was updated. Restart to finish.");
        return;
    }
    }
    step++;
}

void update_tick(u32 now)
{
    net_fetch_t f;
    if (st == UP_CHECKING || st == UP_DOWNLOADING) {
        net_fetch_info(&f);
        if (!t_start) t_start = t_last = now;
        if (st == UP_DOWNLOADING) {
            if (f.len != t_progress_len) { t_progress_len = f.len; t_last = now; }
            pct = size ? (int)(f.len * 90 / size) : 0; if (pct > 90) pct = 90;
        }
        if (f.state == NF_ERROR) { fetch_error(); return; }
        if (now - (st == UP_CHECKING ? t_start : t_last) > (st == UP_CHECKING ? TIMEOUT_MF : TIMEOUT_DL)) {
            net_fetch_abort(); fail(st == UP_CHECKING ? "The update server did not respond." : "The download stalled."); return;
        }
        if (f.state != NF_DONE) return;
        if (f.status != 200) { say("The update server has no update information (HTTP error)."); st = UP_ERROR; return; }
        if (st == UP_CHECKING) {
            mf[f.len < MAN_MAX ? f.len : MAN_MAX - 1] = 0;
            if (update_parse((const char *)mf) != 0) { fail("The update information is damaged."); return; }
            checked = 1; checked_at = now;
            if (build > ENDIX_BUILD) {
                st = UP_AVAILABLE; msg[0] = 0;
                if (auto_on) { char t[48] = "EndixOS "; int i = 8; for (const char *p = ver; *p && i < 40; p++) t[i++] = *p; const char *s = " is available"; for (; *s; s++) t[i++] = *s; t[i] = 0; wm_notify("Software Update", t); }
            } else { st = UP_UPTODATE; msg[0] = 0; }
            return;
        }
        if (f.len != size || f.truncated) { fail("The download is incomplete."); return; }
        if (!same_hash(kbuf, size)) { fail("The downloaded file is damaged (checksum mismatch)."); return; }
        pct = 90; st = UP_INSTALLING; step = 0; return;
    }
    if (st == UP_INSTALLING) { install_step(); return; }
    if (st == UP_IDLE && auto_on && !autodone && now > 2500) {     /* раз за запуск, когда появилась сеть */
        net_info_t ni; net_info(&ni);
        if (ni.up) { autodone = 1; update_check(); }
    }
}
