#include "timeset.h"
#include "../drivers/rtc.h"
#include "../kernel/net.h"
#include "wm.h"

#define TS_URL "https://raw.githubusercontent.com/d9341944-bot/EndixOS/main/endixos-v8/update/latest.txt"

__attribute__((weak)) void rtc_write(const rtc_time_t *t) { (void)t; }       /* хост-тесты; в ядре - drivers/rtc.c */
extern int wm_set_time(u32 hour, u32 min, u32 day, u32 month, u32 year) __attribute__((weak));

static u32 c_h = 12, c_m, c_d = 1, c_mo = 1, c_y = 2026;
static int tz_min, autosync, configured;
static int st = TS_IDLE, attempts;
static u32 last_try;
static char msg[80];
static u8 buf[1024];

void ts_note(u32 h, u32 m, u32 d, u32 mo, u32 y) { c_h = h; c_m = m; c_d = d; c_mo = mo; c_y = y; }
void ts_now(u32 *h, u32 *m, u32 *d, u32 *mo, u32 *y) { *h = c_h; *m = c_m; *d = c_d; *mo = c_mo; *y = c_y; }
int  ts_tz(void) { return tz_min; }
void ts_set_tz(int m) { if (m < -720) m = -720; if (m > 840) m = 840; tz_min = m; configured = 1; }
int  ts_auto(void) { return autosync; }
void ts_set_auto(int on) { autosync = on != 0; configured = 1; attempts = 0; }
int  ts_configured(void) { return configured; }
void ts_load(int tz, int a) { tz_min = tz; autosync = a; configured = 1; }
int  ts_state(void) { return st; }
const char *ts_msg(void) { return msg; }

static void say(const char *s) { int i = 0; while (s[i] && i < (int)sizeof msg - 1) { msg[i] = s[i]; i++; } msg[i] = 0; }

int ts_days_in_month(u32 y, u32 mo)
{
    static const u8 dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (mo < 1 || mo > 12) return 30;
    return dm[mo - 1] + (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0));
}

int ts_from_http_date(const char *s, int tz, u32 *y, u32 *mo, u32 *d, u32 *h, u32 *mi, u32 *sec)
{
    static const char *const mn[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    while (*s && *s != ',') s++;
    if (*s != ',') return -1;
    s++;
    while (*s == ' ') s++;
    u32 dd = 0, yy = 0, hh = 0, mm = 0, ss = 0, mon = 0;
    if (*s < '0' || *s > '9') return -1;
    while (*s >= '0' && *s <= '9') dd = dd * 10 + (u32)(*s++ - '0');
    while (*s == ' ') s++;
    for (u32 i = 0; i < 12; i++) if (s[0] == mn[i][0] && s[1] == mn[i][1] && s[2] == mn[i][2]) mon = i + 1;
    if (!mon) return -1;
    s += 3;
    while (*s == ' ') s++;
    if (*s < '0' || *s > '9') return -1;
    while (*s >= '0' && *s <= '9') yy = yy * 10 + (u32)(*s++ - '0');
    while (*s == ' ') s++;
    for (int k = 0; k < 3; k++) {
        u32 v = 0;
        if (*s < '0' || *s > '9') return -1;
        while (*s >= '0' && *s <= '9') v = v * 10 + (u32)(*s++ - '0');
        if (k == 0) hh = v; else if (k == 1) mm = v; else ss = v;
        if (k < 2) { if (*s != ':') return -1; s++; }
    }
    if (yy < 2020 || yy > 2099 || dd < 1 || dd > 31 || hh > 23 || mm > 59 || ss > 60) return -1;
    int tot = (int)(hh * 60 + mm) + tz;                      /* из UTC в местное */
    int yi = (int)yy, mi_ = (int)mon, di = (int)dd;
    while (tot < 0) { tot += 1440; if (--di < 1) { if (--mi_ < 1) { mi_ = 12; yi--; } di = ts_days_in_month((u32)yi, (u32)mi_); } }
    while (tot >= 1440) { tot -= 1440; if (++di > ts_days_in_month((u32)yi, (u32)mi_)) { di = 1; if (++mi_ > 12) { mi_ = 1; yi++; } } }
    *y = (u32)yi; *mo = (u32)mi_; *d = (u32)di; *h = (u32)tot / 60; *mi = (u32)tot % 60; *sec = ss > 59 ? 59 : ss;
    return 0;
}

static void write_clock(u32 y, u32 mo, u32 d, u32 h, u32 mi, u32 sec)
{
    rtc_time_t t;
    t.sec = (u8)sec; t.min = (u8)mi; t.hour = (u8)h; t.day = (u8)d; t.month = (u8)mo; t.year = (u16)y;
    rtc_write(&t);
    ts_note(h, mi, d, mo, y);
    if (wm_set_time) wm_set_time(h, mi, d, mo, y);
}

int ts_apply(u32 y, u32 mo, u32 d, u32 h, u32 mi)
{
    if (y < 2020 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > (u32)ts_days_in_month(y, mo) || h > 23 || mi > 59) return -1;
    write_clock(y, mo, d, h, mi, 0);
    st = TS_OK; say("Time set.");
    return 0;
}

void ts_sync(void)
{
    if (st == TS_BUSY) return;
    net_info_t ni; net_info(&ni);
    if (!ni.up) { st = TS_FAIL; say("No network connection. Join Wi-Fi or plug in a cable."); return; }
    if (net_fetch(TS_URL, buf, sizeof buf) != 0) { st = TS_FAIL; say("Network is busy, try again in a moment."); return; }
    st = TS_BUSY; say("Contacting the server...");
}

void ts_poll(void)
{
    if (st != TS_BUSY) return;
    net_fetch_t f; net_fetch_info(&f);
    if (f.state == NF_LOADING) return;
    if (f.state == NF_ERROR) { st = TS_FAIL; say(f.err[0] ? f.err : "Could not reach the server."); return; }
    u32 y, mo, d, h, mi, sec;
    if (f.state != NF_DONE || ts_from_http_date(net_fetch_date(), tz_min, &y, &mo, &d, &h, &mi, &sec) != 0) {
        st = TS_FAIL; say("The server did not send the time."); return;
    }
    write_clock(y, mo, d, h, mi, sec);
    st = TS_OK; say("Time synchronized.");
}

void ts_tick(u32 sec)
{
    ts_poll();
    if (!autosync || st == TS_BUSY || attempts >= 4 || sec < 8) return;
    if (attempts && sec - last_try < 20) return;
    net_info_t ni; net_info(&ni);
    if (!ni.up) return;
    if (st == TS_OK) { attempts = 4; return; }
    attempts++; last_try = sec;
    ts_sync();
}
