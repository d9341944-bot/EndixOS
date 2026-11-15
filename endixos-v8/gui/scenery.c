#include "scenery.h"
#include "photos.h"

/* Формат пака: u32 n; n * { u32 off, u32 len, char title[24] }; затем JPEG. */
extern const u8 _binary_wall_pak_start[] __attribute__((weak));
extern const u8 _binary_wall_pak_end[] __attribute__((weak));

static const u8 *pak(u32 *size)
{
    if (!_binary_wall_pak_start || !_binary_wall_pak_end) return 0;
    *size = (u32)(_binary_wall_pak_end - _binary_wall_pak_start);
    return *size >= 4 ? _binary_wall_pak_start : 0;
}
static u32 rd32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }

int scenery_count(void)
{
    u32 sz; const u8 *p = pak(&sz);
    if (!p) return 0;
    u32 n = rd32(p);
    return (n > 16 || 4 + n * 32 > sz) ? 0 : (int)n;
}

const char *scenery_name(int k)
{
    u32 sz; const u8 *p = pak(&sz);
    if (!p || k < 0 || k >= scenery_count()) return "";
    return (const char *)(p + 4 + k * 32 + 8);
}

/* Пары "<Имя> Day" / "<Имя> Night" - «живые» обои: показываются по времени суток (ночь с 19 до 6 часов). */
static int cur_hour = 12;
static int ends(const char *s, const char *suf)
{
    int a = 0, b = 0;
    while (s[a]) a++;
    while (suf[b]) b++;
    if (a < b) return 0;
    for (int i = 0; i < b; i++) if (s[a - b + i] != suf[i]) return 0;
    return 1;
}
int scenery_night(void) { return cur_hour < 6 || cur_hour >= 19; }
int scenery_dynamic(int k) { const char *n = scenery_name(k); return ends(n, " Day") || ends(n, " Night"); }
int scenery_hidden(int k) { return ends(scenery_name(k), " Night"); }     /* в списке только дневной вариант пары */
int scenery_set_hour(int h)                                              /* 1 - день/ночь сменились */
{
    int was = scenery_night();
    cur_hour = h;
    return was != scenery_night();
}
const char *scenery_label(int k)
{
    static char b[2][28];
    static int w;
    char *o = b[w ^= 1];
    const char *n = scenery_name(k);
    int l = 0;
    while (n[l] && l < 20) { o[l] = n[l]; l++; }
    if (scenery_dynamic(k)) {
        l = 0;
        while (n[l] && !(n[l] == ' ' && (ends(n + l, " Day") || ends(n + l, " Night")))) { o[l] = n[l]; l++; }
        const char *d = " (Dynamic)";
        for (int i = 0; d[i]; i++) o[l++] = d[i];
    }
    o[l] = 0;
    return o;
}
static int resolve(int k)                                                /* вариант пары, подходящий времени суток */
{
    if (!scenery_dynamic(k)) return k;
    const char *n = scenery_name(k);
    int base = 0, want_night = scenery_night();
    while (n[base] != ' ' || !(ends(n + base, " Day") || ends(n + base, " Night"))) base++;
    for (int j = 0; j < scenery_count(); j++) {
        const char *m = scenery_name(j);
        int i = 0;
        while (i < base && m[i] == n[i]) i++;
        if (i == base && ends(m, want_night ? " Night" : " Day") && m[base] == ' ') return j;
    }
    return k;
}

int scenery_paint(int k, u32 *dst, u32 w, u32 h)
{
    k = resolve(k);
    u32 sz; const u8 *p = pak(&sz);
    if (!p || k < 0 || k >= scenery_count()) return 0;
    u32 off = rd32(p + 4 + k * 32), len = rd32(p + 8 + k * 32);
    if (off > sz || len > sz - off) return 0;
    return photos_paint_mem(p + off, len, dst, w, h);
}
