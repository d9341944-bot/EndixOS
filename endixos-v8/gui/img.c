/* Декодеры PNG, JPEG (baseline) и GIF. Строки отдаются в уменьшающий накопитель, поэтому большой кадр целиком не хранится. */
#include "img.h"

static void mz(void *d, u32 n) { u8 *a = d; while (n--) *a++ = 0; }
static void mc(void *d, const void *s, u32 n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; }
static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u32 be16(const u8 *p) { return (u32)p[0] << 8 | p[1]; }

/* ---------- уменьшение: усреднение прямоугольников ---------- */

#define MAXW 4096
static struct {
    u32 *out; int sw, sh, tw, th, row, cur_ty;
    u32 r[MAXW], g[MAXW], b[MAXW], n[MAXW];
    u16 xmap[MAXW];
} sc;

static void sc_flush(void)
{
    if (sc.cur_ty < 0) return;
    u32 *o = sc.out + (u32)sc.cur_ty * (u32)sc.tw;
    for (int x = 0; x < sc.tw; x++) {
        u32 c = sc.n[x] ? sc.n[x] : 1;
        o[x] = (sc.r[x] / c) << 16 | (sc.g[x] / c) << 8 | (sc.b[x] / c);
        sc.r[x] = sc.g[x] = sc.b[x] = sc.n[x] = 0;
    }
}

static int sc_begin(u32 *out, int sw, int sh, int tw, int th)
{
    if (sw < 1 || sh < 1 || sw > MAXW || tw < 1 || th < 1 || tw > sw || th > sh) return 0;
    sc.out = out; sc.sw = sw; sc.sh = sh; sc.tw = tw; sc.th = th; sc.row = 0; sc.cur_ty = -1;
    for (int x = 0; x < sw; x++) sc.xmap[x] = (u16)((u32)x * (u32)tw / (u32)sw);
    for (int x = 0; x < tw; x++) sc.r[x] = sc.g[x] = sc.b[x] = sc.n[x] = 0;
    return 1;
}

/* Строка пикселей 0xAARRGGBB; прозрачность смешивается с белым. */
static void sc_row(const u32 *px)
{
    if (sc.row >= sc.sh) return;
    int ty = (int)((u32)sc.row * (u32)sc.th / (u32)sc.sh);
    if (ty != sc.cur_ty) { sc_flush(); sc.cur_ty = ty; }
    for (int x = 0; x < sc.sw; x++) {
        u32 p = px[x], a = p >> 24, r = p >> 16 & 255, g = p >> 8 & 255, b = p & 255;
        if (a != 255) { r = (r * a + 255 * (255 - a)) / 255; g = (g * a + 255 * (255 - a)) / 255; b = (b * a + 255 * (255 - a)) / 255; }
        int tx = sc.xmap[x];
        sc.r[tx] += r; sc.g[tx] += g; sc.b[tx] += b; sc.n[tx]++;
    }
    sc.row++;
}

static void sc_end(void) { sc_flush(); }

/* ---------- inflate (zlib) ---------- */

typedef struct { const u8 *in; u32 n, pos; u32 bitbuf; int bitcnt; u8 *out; u32 cap, olen; int err; } inf_t;
typedef struct { u16 count[16]; u16 sym[288]; } huff_t;

static int getbit(inf_t *s)
{
    if (s->bitcnt == 0) {
        if (s->pos >= s->n) { s->err = 1; return 0; }
        s->bitbuf = s->in[s->pos++]; s->bitcnt = 8;
    }
    int b = (int)(s->bitbuf & 1);
    s->bitbuf >>= 1; s->bitcnt--;
    return b;
}
static u32 getbits(inf_t *s, int n) { u32 v = 0; for (int i = 0; i < n; i++) v |= (u32)getbit(s) << i; return v; }

static int hdecode(inf_t *s, const huff_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= getbit(s);
        int cnt = h->count[len];
        if (code - cnt < first) return h->sym[index + (code - first)];
        index += cnt; first += cnt; first <<= 1; code <<= 1;
        if (s->err) return -1;
    }
    return -1;
}

static void hbuild(huff_t *h, const u8 *len, int n)
{
    u16 offs[16];
    for (int i = 0; i < 16; i++) h->count[i] = 0;
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = (u16)(offs[i] + h->count[i]);
    for (int i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (u16)i;
}

static const u16 LBASE[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const u8  LEXT[29]  = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const u16 DBASE[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const u8  DEXT[30]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static int codes(inf_t *s, const huff_t *lh, const huff_t *dh)
{
    for (;;) {
        int sym = hdecode(s, lh);
        if (sym < 0 || s->err) return -1;
        if (sym < 256) { if (s->olen >= s->cap) return -2; s->out[s->olen++] = (u8)sym; }
        else if (sym == 256) return 0;
        else {
            sym -= 257;
            if (sym >= 29) return -1;
            u32 len = LBASE[sym] + getbits(s, LEXT[sym]);
            int ds = hdecode(s, dh);
            if (ds < 0 || ds >= 30) return -1;
            u32 dist = DBASE[ds] + getbits(s, DEXT[ds]);
            if (dist > s->olen || s->olen + len > s->cap) return -2;
            for (u32 i = 0; i < len; i++) { s->out[s->olen] = s->out[s->olen - dist]; s->olen++; }
        }
    }
}

/* zlib-поток -> out; возвращает длину или -1. */
static i32 zinflate(const u8 *in, u32 n, u8 *out, u32 cap)
{
    inf_t s; mz(&s, sizeof s);
    s.in = in; s.n = n; s.pos = 2; s.out = out; s.cap = cap;       /* пропускаем заголовок zlib */
    static huff_t lh, dh;
    int last;
    do {
        last = getbit(&s);
        u32 type = getbits(&s, 2);
        if (s.err) return -1;
        if (type == 0) {
            s.bitcnt = 0;
            if (s.pos + 4 > n) return -1;
            u32 len = in[s.pos] | (u32)in[s.pos + 1] << 8;
            s.pos += 4;
            if (s.pos + len > n || s.olen + len > cap) return -1;
            mc(out + s.olen, in + s.pos, len); s.olen += len; s.pos += len;
        } else if (type == 1) {
            u8 l[288];
            for (int i = 0; i < 144; i++) l[i] = 8;
            for (int i = 144; i < 256; i++) l[i] = 9;
            for (int i = 256; i < 280; i++) l[i] = 7;
            for (int i = 280; i < 288; i++) l[i] = 8;
            hbuild(&lh, l, 288);
            u8 d[30]; for (int i = 0; i < 30; i++) d[i] = 5;
            hbuild(&dh, d, 30);
            if (codes(&s, &lh, &dh)) return -1;
        } else if (type == 2) {
            int nlen = (int)getbits(&s, 5) + 257, ndist = (int)getbits(&s, 5) + 1, ncode = (int)getbits(&s, 4) + 4;
            static const u8 ord[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
            u8 lens[320]; huff_t ch;
            mz(lens, sizeof lens);
            if (nlen > 286 || ndist > 30) return -1;
            for (int i = 0; i < ncode; i++) lens[ord[i]] = (u8)getbits(&s, 3);
            hbuild(&ch, lens, 19);
            u8 all[320]; mz(all, sizeof all);
            int idx = 0;
            while (idx < nlen + ndist) {
                int sym = hdecode(&s, &ch);
                if (sym < 0) return -1;
                if (sym < 16) all[idx++] = (u8)sym;
                else {
                    int rep, val = 0;
                    if (sym == 16) { if (idx == 0) return -1; val = all[idx - 1]; rep = 3 + (int)getbits(&s, 2); }
                    else if (sym == 17) rep = 3 + (int)getbits(&s, 3);
                    else rep = 11 + (int)getbits(&s, 7);
                    if (idx + rep > nlen + ndist) return -1;
                    while (rep--) all[idx++] = (u8)val;
                }
            }
            hbuild(&lh, all, nlen);
            hbuild(&dh, all + nlen, ndist);
            if (codes(&s, &lh, &dh)) return -1;
        } else return -1;
        if (s.err) return -1;
    } while (!last);
    return (i32)s.olen;
}

/* ---------- PNG ---------- */

#define WORK_SZ (5 * 1024 * 1024)
static u8 work[WORK_SZ];
static u32 rowpx[MAXW];

static int png_parse(const u8 *d, u32 n, int *w, int *h, int *ct, int *bd, int *il)
{
    if (n < 33 || d[0] != 0x89 || d[1] != 'P' || d[2] != 'N' || d[3] != 'G') return 0;
    if (be32(d + 8) != 13 || d[12] != 'I') return 0;
    *w = (int)be32(d + 16); *h = (int)be32(d + 20); *bd = d[24]; *ct = d[25]; *il = d[28];
    return 1;
}

static int png_decode(const u8 *d, u32 n, int tw, int th, u32 *out)
{
    int w, h, ct, bd, il;
    if (!png_parse(d, n, &w, &h, &ct, &bd, &il) || il) return 0;
    int ch = ct == 0 ? 1 : ct == 2 ? 3 : ct == 3 ? 1 : ct == 4 ? 2 : ct == 6 ? 4 : 0;
    if (!ch || w < 1 || h < 1 || (u32)w * (u32)h > IMG_MAX_PIXELS || w > MAXW) return 0;
    u32 bpp_bits = (u32)ch * (u32)bd, stride = ((u32)w * bpp_bits + 7) / 8, bpp = (bpp_bits + 7) / 8;
    u32 need = (stride + 1) * (u32)h;
    static u8 *idat; static u32 idat_len;
    static u8 plte[768], trns[256]; int ntrns = 0, nplte = 0;
    u32 key_r = 0x10000, key_g = 0, key_b = 0, key_gray = 0x10000;
    /* IDAT собираем в хвост рабочей области, inflate пишет в начало */
    if (need + 16 > WORK_SZ) return 0;
    idat = work + WORK_SZ - 1; idat_len = 0;
    u32 total_idat = 0;
    for (u32 p = 8; p + 12 <= n;) {
        u32 len = be32(d + p);
        if (len > n - p - 12) break;
        const u8 *c = d + p + 4, *dat = d + p + 8;
        if (c[0] == 'I' && c[1] == 'D' && c[2] == 'A' && c[3] == 'T') total_idat += len;
        else if (c[0] == 'P' && c[1] == 'L') { nplte = (int)(len / 3); if (nplte > 256) nplte = 256; mc(plte, dat, (u32)nplte * 3); }
        else if (c[0] == 't' && c[1] == 'R') {
            if (ct == 3) { ntrns = (int)(len > 256 ? 256 : len); mc(trns, dat, (u32)ntrns); }
            else if (ct == 0 && len >= 2) key_gray = be16(dat);
            else if (ct == 2 && len >= 6) { key_r = be16(dat); key_g = be16(dat + 2); key_b = be16(dat + 4); }
        }
        p += 12 + len;
    }
    if (!total_idat || need + total_idat + 16 > WORK_SZ) return 0;
    u8 *buf = work + WORK_SZ - total_idat;
    u32 bl = 0;
    for (u32 p = 8; p + 12 <= n;) {
        u32 len = be32(d + p);
        if (len > n - p - 12) break;
        if (d[p+4] == 'I' && d[p+5] == 'D' && d[p+6] == 'A' && d[p+7] == 'T') { mc(buf + bl, d + p + 8, len); bl += len; }
        p += 12 + len;
    }
    (void)idat; (void)idat_len;
    i32 got = zinflate(buf, bl, work, need);
    if (got < (i32)need) return 0;
    if (!sc_begin(out, w, h, tw, th)) return 0;
    u8 *prev = 0;
    u8 zero[MAXW * 8]; (void)zero;
    for (int y = 0; y < h; y++) {
        u8 *row = work + (u32)y * (stride + 1);
        u8 f = row[0]; u8 *cur = row + 1;
        for (u32 i = 0; i < stride; i++) {
            int a = i >= bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= bpp) ? prev[i - bpp] : 0, x = cur[i], v;
            switch (f) {
            case 1: v = x + a; break;
            case 2: v = x + b; break;
            case 3: v = x + ((a + b) >> 1); break;
            case 4: { int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
                      v = x + ((pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c)); break; }
            default: v = x; break;
            }
            cur[i] = (u8)v;
        }
        for (int x = 0; x < w; x++) {
            u32 r, g, b, a = 255;
            if (ct == 3) {
                u32 idx = bd == 8 ? cur[x] : (cur[(u32)x * (u32)bd / 8] >> (8 - bd - ((u32)x * (u32)bd % 8))) & ((1u << bd) - 1);
                r = idx < (u32)nplte ? plte[idx * 3] : 0; g = idx < (u32)nplte ? plte[idx * 3 + 1] : 0; b = idx < (u32)nplte ? plte[idx * 3 + 2] : 0;
                if ((int)idx < ntrns) a = trns[idx];
            } else if (ct == 0 || ct == 4) {
                u32 v16;
                if (bd == 16) { v16 = be16(cur + (u32)x * (u32)ch * 2); r = cur[(u32)x * (u32)ch * 2]; if (ct == 4) a = cur[(u32)x * 4 + 2]; }
                else if (bd == 8) { v16 = cur[(u32)x * (u32)ch]; r = v16; if (ct == 4) a = cur[(u32)x * 2 + 1]; }
                else { u32 raw = (cur[(u32)x * (u32)bd / 8] >> (8 - bd - ((u32)x * (u32)bd % 8))) & ((1u << bd) - 1); v16 = raw; r = raw * 255 / ((1u << bd) - 1); }
                if (ct == 0 && key_gray != 0x10000 && v16 == key_gray) a = 0;
                g = b = r;
            } else {
                u32 o = (u32)x * (u32)ch * (u32)(bd / 8);
                if (bd == 16) { r = cur[o]; g = cur[o + 2]; b = cur[o + 4]; if (ct == 6) a = cur[o + 6]; if (ct == 2 && key_r != 0x10000 && be16(cur + o) == key_r && be16(cur + o + 2) == key_g && be16(cur + o + 4) == key_b) a = 0; }
                else { r = cur[o]; g = cur[o + 1]; b = cur[o + 2]; if (ct == 6) a = cur[o + 3]; if (ct == 2 && key_r != 0x10000 && r == key_r && g == key_g && b == key_b) a = 0; }
            }
            rowpx[x] = a << 24 | r << 16 | g << 8 | b;
        }
        sc_row(rowpx);
        prev = cur;
    }
    sc_end();
    return 1;
}

/* ---------- GIF ---------- */

static int gif_info(const u8 *d, u32 n, int *w, int *h)
{
    if (n < 13 || d[0] != 'G' || d[1] != 'I' || d[2] != 'F') return 0;
    *w = d[6] | d[7] << 8; *h = d[8] | d[9] << 8;
    return 1;
}

static int gif_decode(const u8 *d, u32 n, int tw, int th, u32 *out)
{
    int sw, sh;
    if (!gif_info(d, n, &sw, &sh) || sw < 1 || sh < 1 || (u32)sw * (u32)sh > IMG_MAX_PIXELS || sw > MAXW) return 0;
    u32 p = 13;
    static u32 gpal[256], lpal[256];
    int gn = 0;
    if (d[10] & 0x80) { gn = 2 << (d[10] & 7); if (p + (u32)gn * 3 > n) return 0; for (int i = 0; i < gn; i++) gpal[i] = 0xFF000000u | (u32)d[p + i*3] << 16 | (u32)d[p + i*3 + 1] << 8 | d[p + i*3 + 2]; p += (u32)gn * 3; }
    int trans = -1;
    while (p < n) {
        u8 b = d[p++];
        if (b == 0x21) {
            if (p >= n) return 0;
            u8 label = d[p++];
            if (label == 0xF9 && p + 5 < n && d[p] >= 4 && (d[p + 1] & 1)) trans = d[p + 4];
            while (p < n && d[p]) p += (u32)d[p] + 1;
            p++;
        } else if (b == 0x2C) {
            if (p + 9 > n) return 0;
            int ix = d[p] | d[p+1] << 8, iy = d[p+2] | d[p+3] << 8, iw = d[p+4] | d[p+5] << 8, ih = d[p+6] | d[p+7] << 8;
            u8 fl = d[p + 8]; p += 9;
            u32 *pal = gpal; int pn = gn;
            if (fl & 0x80) { pn = 2 << (fl & 7); if (p + (u32)pn * 3 > n) return 0; for (int i = 0; i < pn; i++) lpal[i] = 0xFF000000u | (u32)d[p + i*3] << 16 | (u32)d[p + i*3 + 1] << 8 | d[p + i*3 + 2]; pal = lpal; p += (u32)pn * 3; }
            if (p >= n || iw < 1 || ih < 1 || (u32)iw * (u32)ih > WORK_SZ) return 0;
            int minc = d[p++];
            if (minc < 1 || minc > 11) return 0;
            /* LZW */
            static u16 prefix[4096]; static u8 suffix[4096], stack[4097];
            int clear = 1 << minc, eoi = clear + 1, next = clear + 2, size = minc + 1, old = -1;
            u32 bits = 0; int nb = 0; u32 outn = 0, total = (u32)iw * (u32)ih;
            for (int i = 0; i < clear; i++) { prefix[i] = 0; suffix[i] = (u8)i; }
            u8 first = 0;
            u32 bp = p; u32 blk_end = p;                   /* чтение подблоков */
            int done = 0;
            while (!done && outn < total) {
                while (nb < size) {
                    if (bp >= blk_end) { if (bp >= n) { done = 1; break; } u8 L = d[bp++]; if (!L) { done = 1; break; } blk_end = bp + L; if (blk_end > n) { done = 1; break; } }
                    bits |= (u32)d[bp++] << nb; nb += 8;
                }
                if (done) break;
                int code = (int)(bits & ((1u << size) - 1)); bits >>= size; nb -= size;
                if (code == clear) { next = clear + 2; size = minc + 1; old = -1; continue; }
                if (code == eoi) break;
                int cur = code, sp = 0;
                if (old < 0) { if (code >= clear) return 0; work[outn++] = suffix[code]; first = suffix[code]; old = code; continue; }
                if (code >= next) { if (code > next) return 0; stack[sp++] = first; cur = old; }
                while (cur >= clear) { if (sp >= 4096) return 0; stack[sp++] = suffix[cur]; cur = prefix[cur]; }
                first = suffix[cur]; stack[sp++] = first;
                while (sp && outn < total) work[outn++] = stack[--sp];
                if (next < 4096) { prefix[next] = (u16)old; suffix[next] = first; next++; if (next == (1 << size) && size < 12) size++; }
                old = code;
            }
            if (outn < total) { for (u32 i = outn; i < total; i++) work[i] = 0; }
            if (!sc_begin(out, sw, sh, tw, th)) return 0;
            u32 order[256 * 0 + 1]; (void)order;
            for (int y = 0; y < sh; y++) {
                int sy = y - iy;
                if (sy >= 0 && sy < ih && (fl & 0x40)) {           /* чересстрочный порядок: найти строку источника */
                    int pass_start[4] = { 0, 4, 2, 1 }, pass_step[4] = { 8, 8, 4, 2 }, cnt = 0, src = -1;
                    for (int ps = 0; ps < 4 && src < 0; ps++)
                        for (int r = pass_start[ps]; r < ih; r += pass_step[ps], cnt++) if (r == sy) { src = cnt; break; }
                    sy = src;
                }
                for (int x = 0; x < sw; x++) {
                    int sx = x - ix; u32 px = 0xFFFFFFFFu;
                    if (sy >= 0 && sy < ih && sx >= 0 && sx < iw) {
                        u8 idx = work[(u32)sy * (u32)iw + (u32)sx];
                        px = (idx == trans || idx >= pn) ? 0x00FFFFFFu : pal[idx];
                    }
                    rowpx[x] = px;
                }
                sc_row(rowpx);
            }
            sc_end();
            return 1;
        } else if (b == 0x3B) break;
        else return 0;
    }
    return 0;
}

/* ---------- JPEG baseline ---------- */

typedef struct { u8 bits[17]; u8 vals[256]; u16 mincode[17]; i32 maxcode[18]; u16 valptr[17]; } jh_t;
static struct {
    const u8 *d; u32 n, pos;
    u16 qt[4][64];
    jh_t dc[4], ac[4];
    int w, h, nc, restart;
    struct { int id, hs, vs, tq, td, ta; i32 pred; } c[4];
    u32 bitbuf; int bitcnt; int marker_hit;
} jp;

static const u8 ZZ[64] = { 0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63 };

static int jbit(void)
{
    if (jp.bitcnt == 0) {
        u8 b = 0;
        if (jp.pos < jp.n && !jp.marker_hit) {
            b = jp.d[jp.pos++];
            if (b == 0xFF) {
                u8 b2 = jp.pos < jp.n ? jp.d[jp.pos] : 0;
                if (b2 == 0) jp.pos++;
                else { jp.marker_hit = 1; jp.pos--; b = 0; }
            }
        }
        jp.bitbuf = b; jp.bitcnt = 8;
    }
    jp.bitcnt--;
    return (int)((jp.bitbuf >> jp.bitcnt) & 1);
}
static int jbits(int n) { int v = 0; while (n--) v = v << 1 | jbit(); return v; }

static void jbuild(jh_t *h)
{
    int k = 0; i32 code = 0;
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = (u16)k;
        h->mincode[l] = (u16)code;
        code += h->bits[l]; k += h->bits[l];
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
}

static int jdec(const jh_t *h)
{
    i32 code = 0;
    for (int l = 1; l <= 16; l++) {
        code = code << 1 | jbit();
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l]) return h->vals[h->valptr[l] + (u32)(code - h->mincode[l])];
    }
    return -1;
}

static int jext(int v, int t) { return t && v < (1 << (t - 1)) ? v - (1 << t) + 1 : v; }

#define F2F(x) ((int)((x) * 4096 + 0.5))
static u8 clamp8(int v) { return (u8)(v < 0 ? 0 : v > 255 ? 255 : v); }

#define IDCT1D(s0,s1,s2,s3,s4,s5,s6,s7) \
    int t0, t1, t2, t3, p1, p2, p3, p4, p5, x0, x1, x2, x3; \
    p2 = s2; p3 = s6; p1 = (p2 + p3) * 2217; t2 = p1 + p3 * -7567; t3 = p1 + p2 * 3135; \
    p2 = s0; p3 = s4; t0 = (p2 + p3) * 4096; t1 = (p2 - p3) * 4096; \
    x0 = t0 + t3; x3 = t0 - t3; x1 = t1 + t2; x2 = t1 - t2; \
    t0 = s7; t1 = s5; t2 = s3; t3 = s1; \
    p3 = t0 + t2; p4 = t1 + t3; p1 = t0 + t3; p2 = t1 + t2; p5 = (p3 + p4) * 4816; \
    t0 = t0 * 1223; t1 = t1 * 8410; t2 = t2 * 12586; t3 = t3 * 6149; \
    p1 = p5 + p1 * -3685; p2 = p5 + p2 * -10497; p3 = p3 * -8034; p4 = p4 * -1597; \
    t3 += p1 + p4; t2 += p2 + p3; t1 += p2 + p4; t0 += p1 + p3;

static void idct(u8 *out, int stride, const i16 *dd)
{
    int val[64], *v = val;
    const i16 *d = dd;
    for (int i = 0; i < 8; i++, d++, v++) {
        if (!d[8] && !d[16] && !d[24] && !d[32] && !d[40] && !d[48] && !d[56]) {
            int dc = d[0] * 4;
            v[0] = v[8] = v[16] = v[24] = v[32] = v[40] = v[48] = v[56] = dc;
        } else {
            IDCT1D(d[0], d[8], d[16], d[24], d[32], d[40], d[48], d[56])
            x0 += 512; x1 += 512; x2 += 512; x3 += 512;
            v[0] = (x0 + t3) >> 10; v[56] = (x0 - t3) >> 10; v[8] = (x1 + t2) >> 10; v[48] = (x1 - t2) >> 10;
            v[16] = (x2 + t1) >> 10; v[40] = (x2 - t1) >> 10; v[24] = (x3 + t0) >> 10; v[32] = (x3 - t0) >> 10;
        }
    }
    v = val;
    for (int i = 0; i < 8; i++, v += 8, out += stride) {
        IDCT1D(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7])
        x0 += 65536 + (128 << 17); x1 += 65536 + (128 << 17); x2 += 65536 + (128 << 17); x3 += 65536 + (128 << 17);
        out[0] = clamp8((x0 + t3) >> 17); out[7] = clamp8((x0 - t3) >> 17); out[1] = clamp8((x1 + t2) >> 17); out[6] = clamp8((x1 - t2) >> 17);
        out[2] = clamp8((x2 + t1) >> 17); out[5] = clamp8((x2 - t1) >> 17); out[3] = clamp8((x3 + t0) >> 17); out[4] = clamp8((x3 - t0) >> 17);
    }
}

/* Разбор маркеров до SOS. Возвращает 1, если готово к декодированию данных. */
static int jpeg_headers(const u8 *d, u32 n)
{
    mz(&jp, sizeof jp);
    jp.d = d; jp.n = n;
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return 0;
    u32 p = 2;
    int sof = 0;
    while (p + 4 <= n) {
        if (d[p] != 0xFF) { p++; continue; }
        u8 m = d[p + 1];
        if (m == 0xFF) { p++; continue; }
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01 || m == 0) { p += 2; continue; }
        u32 len = be16(d + p + 2);
        if (len < 2 || p + 2 + len > n) return 0;
        const u8 *s = d + p + 4; u32 sl = len - 2;
        if (m == 0xDB) {
            while (sl >= 1) {
                int pq = s[0] >> 4, tq = s[0] & 15; s++; sl--;
                if (tq > 3 || sl < (u32)(pq ? 128 : 64)) return 0;
                for (int i = 0; i < 64; i++) { jp.qt[tq][ZZ[i]] = pq ? (u16)be16(s + i * 2) : s[i]; }
                s += pq ? 128 : 64; sl -= pq ? 128 : 64;
            }
        } else if (m == 0xC4) {
            while (sl >= 17) {
                int tc = s[0] >> 4, th = s[0] & 15;
                if (th > 3 || tc > 1) return 0;
                jh_t *h = tc ? &jp.ac[th] : &jp.dc[th];
                int cnt = 0;
                for (int i = 1; i <= 16; i++) { h->bits[i] = s[i]; cnt += s[i]; }
                if (cnt > 256 || sl < 17 + (u32)cnt) return 0;
                for (int i = 0; i < cnt; i++) h->vals[i] = s[17 + i];
                jbuild(h);
                s += 17 + cnt; sl -= 17 + (u32)cnt;
            }
        } else if (m == 0xC0 || m == 0xC1) {
            if (sl < 6 || s[0] != 8) return 0;
            jp.h = (int)be16(s + 1); jp.w = (int)be16(s + 3); jp.nc = s[5];
            if ((jp.nc != 1 && jp.nc != 3) || sl < 6 + (u32)jp.nc * 3) return 0;
            for (int i = 0; i < jp.nc; i++) { jp.c[i].id = s[6 + i*3]; jp.c[i].hs = s[7 + i*3] >> 4; jp.c[i].vs = s[7 + i*3] & 15; jp.c[i].tq = s[8 + i*3]; if (jp.c[i].hs < 1 || jp.c[i].hs > 4 || jp.c[i].vs < 1 || jp.c[i].vs > 4 || jp.c[i].tq > 3) return 0; }
            sof = 1;
        } else if (m == 0xC2 || (m >= 0xC3 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)) return 0;    /* прогрессивные и прочие */
        else if (m == 0xDD) { if (sl >= 2) jp.restart = (int)be16(s); }
        else if (m == 0xDA) {
            if (!sof || sl < 1 + (u32)jp.nc * 2) return 0;
            for (int i = 0; i < jp.nc; i++) {
                int id = s[1 + i*2], tb = s[2 + i*2];
                int k = 0; while (k < jp.nc && jp.c[k].id != id) k++;
                if (k == jp.nc) return 0;
                jp.c[k].td = tb >> 4; jp.c[k].ta = tb & 15;
                if (jp.c[k].td > 3 || jp.c[k].ta > 3) return 0;
            }
            jp.pos = p + 2 + len;
            return 1;
        }
        p += 2 + len;
    }
    return 0;
}

static u8 jplane[3][MAXW * 16 + 64];

static int jpeg_decode(const u8 *d, u32 n, int tw, int th, u32 *out)
{
    if (!jpeg_headers(d, n)) return 0;
    int w = jp.w, h = jp.h;
    if (w < 1 || h < 1 || w > MAXW || (u32)w * (u32)h > 6 * 1000 * 1000) return 0;
    int hmax = 1, vmax = 1;
    for (int i = 0; i < jp.nc; i++) { if (jp.c[i].hs > hmax) hmax = jp.c[i].hs; if (jp.c[i].vs > vmax) vmax = jp.c[i].vs; }
    int mw = hmax * 8, mh = vmax * 8, mx = (w + mw - 1) / mw, my = (h + mh - 1) / mh;
    if ((u32)mx * (u32)mw > MAXW) return 0;
    if (!sc_begin(out, w, h, tw, th)) return 0;
    i16 blk[64];
    int rst = 0;
    for (int yy = 0; yy < my; yy++) {
        for (int xx = 0; xx < mx; xx++) {
            if (jp.restart && rst == jp.restart) {                       /* маркер RSTn: сбрасываем разность DC и биты */
                jp.bitcnt = 0; jp.marker_hit = 0;
                while (jp.pos + 1 < jp.n && !(jp.d[jp.pos] == 0xFF && jp.d[jp.pos + 1] >= 0xD0 && jp.d[jp.pos + 1] <= 0xD7)) jp.pos++;
                jp.pos += 2; rst = 0;
                for (int i = 0; i < jp.nc; i++) jp.c[i].pred = 0;
            }
            rst++;
            for (int ci = 0; ci < jp.nc; ci++) {
                for (int by = 0; by < jp.c[ci].vs; by++) for (int bx = 0; bx < jp.c[ci].hs; bx++) {
                    mz(blk, sizeof blk);
                    int t = jdec(&jp.dc[jp.c[ci].td]);
                    if (t < 0 || t > 15) return 0;
                    int diff = t ? jext(jbits(t), t) : 0;
                    jp.c[ci].pred += diff;
                    const u16 *q = jp.qt[jp.c[ci].tq];
                    blk[0] = (i16)(jp.c[ci].pred * q[0]);
                    for (int k = 1; k < 64;) {
                        int rs = jdec(&jp.ac[jp.c[ci].ta]);
                        if (rs < 0) return 0;
                        int r = rs >> 4, s = rs & 15;
                        if (!s) { if (r != 15) break; k += 16; continue; }
                        k += r;
                        if (k > 63) break;
                        blk[ZZ[k]] = (i16)(jext(jbits(s), s) * q[ZZ[k]]);
                        k++;
                    }
                    int stride = mx * jp.c[ci].hs * 8;
                    idct(jplane[ci] + (u32)(by * 8) * (u32)stride + (u32)((xx * jp.c[ci].hs + bx) * 8), stride, blk);
                }
            }
        }
        for (int ly = 0; ly < mh; ly++) {
            int y = yy * mh + ly;
            if (y >= h) break;
            for (int x = 0; x < w; x++) {
                int Y, cb = 128, cr = 128;
                int s0 = mx * jp.c[0].hs * 8;
                Y = jplane[0][(u32)(ly * jp.c[0].vs / vmax) * (u32)s0 + (u32)(x * jp.c[0].hs / hmax)];
                if (jp.nc == 3) {
                    int s1 = mx * jp.c[1].hs * 8, s2 = mx * jp.c[2].hs * 8;
                    cb = jplane[1][(u32)(ly * jp.c[1].vs / vmax) * (u32)s1 + (u32)(x * jp.c[1].hs / hmax)];
                    cr = jplane[2][(u32)(ly * jp.c[2].vs / vmax) * (u32)s2 + (u32)(x * jp.c[2].hs / hmax)];
                }
                u32 r = clamp8(Y + (91881 * (cr - 128) >> 16)), g = clamp8(Y - ((22554 * (cb - 128) + 46802 * (cr - 128)) >> 16)), b = clamp8(Y + (116130 * (cb - 128) >> 16));
                if (jp.nc == 1) r = g = b = (u32)Y;
                rowpx[x] = 0xFF000000u | r << 16 | g << 8 | b;
            }
            sc_row(rowpx);
        }
    }
    sc_end();
    return 1;
}

/* ---------- общий вход ---------- */

int img_info(const u8 *d, u32 n, int *w, int *h)
{
    int ct, bd, il;
    if (n > 8 && d[0] == 0x89 && d[1] == 'P') { if (png_parse(d, n, w, h, &ct, &bd, &il) && !il) return IMG_PNG; return 0; }
    if (n > 3 && d[0] == 0xFF && d[1] == 0xD8) { if (jpeg_headers(d, n)) { *w = jp.w; *h = jp.h; return IMG_JPEG; } return 0; }
    if (n > 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F') { if (gif_info(d, n, w, h)) return IMG_GIF; return 0; }
    return 0;
}

int img_decode(const u8 *d, u32 n, int tw, int th, u32 *out) { return img_decode_ex(d, n, tw, th, out, IMG_MAX_PIXELS * 3); }

int img_decode_ex(const u8 *d, u32 n, int tw, int th, u32 *out, u32 maxpix)
{
    int w, h, f = img_info(d, n, &w, &h);
    if (!f || (u32)w * (u32)h > maxpix) return 0;
    if (f == IMG_PNG) return png_decode(d, n, tw, th, out);
    if (f == IMG_JPEG) return jpeg_decode(d, n, tw, th, out);
    return gif_decode(d, n, tw, th, out);
}
