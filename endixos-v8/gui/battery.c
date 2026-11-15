#include "battery.h"
#include "fb.h"
#include "font.h"
#include "../kernel/ec.h"

__attribute__((weak)) int ec_read(u8 off, u8 *v) { (void)off; (void)v; return -1; }
__attribute__((weak)) int ec_dump(u8 out[256]) { int n = 0; for (int i = 0; i < 256; i++) { if (ec_read((u8)i, &out[i])) break; n++; } return n; }

static int pct_off = -1, ac_off = -1, ac_mask = 1;
static int rc_off = -1;                         /* автоопределение: слово остатка (RC) и сразу за ним слово полной ёмкости (FCC) */
static int auto_tried, auto_cnt, auto_best = -1;
static int last_rc = -1, trend;                 /* тренд заряда: +1 растёт (адаптер), -1 падает */
static int pct = -1, ac = -1;
static u32 last;

int bat_configured(void) { return pct_off >= 0 || rc_off >= 0; }
int bat_percent(void) { return pct; }
int bat_ac(void) { return ac; }

void bat_config(int p, int a, int m)
{
    pct_off = p < 0 || p > 255 ? -1 : p;
    rc_off = -1; auto_tried = 1;                  /* ручная настройка отключает автоопределение */
    ac_off = a < 0 || a > 255 ? -1 : a;
    ac_mask = m > 0 && m < 256 ? m : 1;
    pct = ac = -1; last = 0;
}

void bat_get_config(int *p, int *a, int *m) { *p = pct_off; *a = ac_off; *m = ac_mask; }

/* Ищет в EC пары слов «остаток, полная ёмкость» (мАч или мВтч) рядом со словом напряжения: так устроен почти любой блок данных батареи.
 * Берёт кандидата только если он один; иначе остаёмся без значка (лучше ничего, чем неверный процент). */
static int le16(const u8 *e, int o) { return e[o] | e[o + 1] << 8; }

int bat_autodetect(void)
{
    static u8 e[256];
    if (ec_dump(e) != 256) return 0;
    auto_cnt = 0; auto_best = -1;
    for (int o = 0; o + 3 < 256; o++) {
        int rc = le16(e, o), fc = le16(e, o + 2);
        if (fc < 1000 || fc > 12000 || rc < fc / 50 || rc > fc) continue;
        int volt = 0;
        for (int j = o - 12; j <= o + 16 && !volt; j++) {
            if (j < 0 || j + 1 >= 256 || (j + 1 >= o && j <= o + 3)) continue;
            int v = le16(e, j);
            if (v >= 5000 && v <= 13500) volt = 1;
        }
        if (!volt) continue;
        if (auto_cnt && o <= auto_best + 1) continue;           /* соседний сдвиг той же пары */
        auto_cnt++; auto_best = o;
        if (auto_cnt == 1) rc_off = o;
    }
    if (auto_cnt != 1) rc_off = -1;
    pct = -1; last = 0; last_rc = -1; trend = 0;
    return rc_off >= 0;
}

int bat_autodetect_info(int *count) { *count = auto_cnt; return rc_off; }

int bat_poll(u32 ticks)
{
    if (pct_off < 0 && rc_off < 0) {
        if (!auto_tried && ticks > 600) { auto_tried = 1; bat_autodetect(); }
        if (rc_off < 0) return 0;
    }
    if (pct >= 0 && (u32)(ticks - last) < 500) return 0;      /* 100 Гц: раз в 5 с */
    last = ticks;
    u8 v; int np = -1, na = -1;
    if (rc_off >= 0) {
        u8 a, b, c, d;
        if (ec_read((u8)rc_off, &a) == 0 && ec_read((u8)(rc_off + 1), &b) == 0 && ec_read((u8)(rc_off + 2), &c) == 0 && ec_read((u8)(rc_off + 3), &d) == 0) {
            int rc = a | b << 8, fc = c | d << 8;
            if (fc >= 500 && fc <= 20000 && rc <= fc + fc / 20) np = rc >= fc ? 100 : rc * 100 / fc;
            if (last_rc >= 0 && rc != last_rc) trend = rc > last_rc ? 1 : -1;
            last_rc = rc;
            na = trend > 0 ? 1 : trend < 0 ? 0 : -1;
        }
    } else {
        if (ec_read((u8)pct_off, &v) == 0 && v <= 100) np = v;
        if (ac_off >= 0 && ec_read((u8)ac_off, &v) == 0) na = (v & ac_mask) ? 1 : 0;
    }
    int ch = np != pct || na != ac;
    pct = np; ac = na;
    return ch;
}

i32 bat_width(void)
{
    if (pct < 0) return 0;
    char t[6]; int n = 0;
    if (pct >= 100) t[n++] = '1';
    if (pct >= 10) t[n++] = (char)('0' + pct / 10 % 10);
    t[n++] = (char)('0' + pct % 10); t[n++] = '%'; t[n] = 0;
    return 26 + 6 + font_width(t, FONT_REGULAR);
}

void bat_draw(i32 x, i32 y, u32 ink)
{
    if (pct < 0) return;
    char t[6]; int n = 0;
    if (pct >= 100) t[n++] = '1';
    if (pct >= 10) t[n++] = (char)('0' + pct / 10 % 10);
    t[n++] = (char)('0' + pct % 10); t[n++] = '%'; t[n] = 0;
    i32 by = y + 5;                                           /* корпус 22x11, носик справа */
    fb_fill_round_rect(x, by, 22, 11, 3, ink, 255);
    fb_fill_round_rect(x + 1, by + 1, 20, 9, 2, RGB(0, 0, 0), 120);
    fb_fill_rect(x + 22, by + 3, 2, 5, ink);
    i32 fw = 18 * pct / 100;
    if (fw < 1 && pct > 0) fw = 1;
    u32 col = pct <= 15 && ac != 1 ? RGB(255, 69, 58) : ac == 1 ? RGB(48, 209, 88) : ink;
    if (fw > 0) fb_fill_round_rect(x + 2, by + 2, fw, 7, 1, col, 255);
    if (ac == 1) {                                            /* молния на корпусе */
        static const u8 bolt[7][2] = { { 5, 6 }, { 4, 5 }, { 3, 4 }, { 2, 6 }, { 4, 5 }, { 3, 4 }, { 2, 3 } };
        for (int r = 0; r < 7; r++) fb_fill_rect(x + 6 + bolt[r][0], by + 2 + r, bolt[r][1] - bolt[r][0] + 1, 1, RGB(255, 255, 255));
    }
    font_draw(x + 30, y + 3, t, ink, FONT_REGULAR);
}
