#include "blk.h"

static void zero(u8 *p, u32 n) { while (n--) *p++ = 0; }
static void put32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static u32 get32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

/* CHS для больших дисков не нужен: пишем 0xFE,0xFF,0xFF, как делают fdisk и parted. */
int mbr_set_single(blk_t *b, u32 start, u32 size, u8 type)
{
    u8 s[512];
    if (b->rd(b->ctx, 0, 1, s)) zero(s, 512);
    if (s[510] != 0x55 || s[511] != 0xAA) zero(s, 512);
    zero(s + 446, 64);
    u8 *e = s + 446;
    e[0] = 0x80;                                           /* активный */
    e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF;
    e[4] = type;
    e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
    put32(e + 8, start); put32(e + 12, size);
    s[510] = 0x55; s[511] = 0xAA;
    return b->wr(b->ctx, 0, 1, s);
}

int mbr_set_code(blk_t *b, const u8 *code)
{
    u8 s[512];
    if (b->rd(b->ctx, 0, 1, s)) return -1;
    for (int i = 0; i < 440; i++) s[i] = code[i];
    s[510] = 0x55; s[511] = 0xAA;
    return b->wr(b->ctx, 0, 1, s);
}

int mbr_get_part(blk_t *b, int n, u8 *type, u32 *start, u32 *size)
{
    u8 s[512];
    if (n < 0 || n > 3 || b->rd(b->ctx, 0, 1, s)) return -1;
    if (s[510] != 0x55 || s[511] != 0xAA) return -1;
    const u8 *e = s + 446 + n * 16;
    if (!e[4]) return -1;
    *type = e[4]; *start = get32(e + 8); *size = get32(e + 12);
    return 0;
}
