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

int scenery_paint(int k, u32 *dst, u32 w, u32 h)
{
    u32 sz; const u8 *p = pak(&sz);
    if (!p || k < 0 || k >= scenery_count()) return 0;
    u32 off = rd32(p + 4 + k * 32), len = rd32(p + 8 + k * 32);
    if (off > sz || len > sz - off) return 0;
    return photos_paint_mem(p + off, len, dst, w, h);
}
