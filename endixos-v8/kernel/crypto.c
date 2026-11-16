/* SHA-256, HMAC, TLS 1.2 PRF, AES-128-GCM, X25519 и генератор случайных. Без libgcc: только 32-битные операции
 * (в X25519 только умножения и сдвиги 64-битных, без деления). */
#include "crypto.h"

static void mcopy(void *d, const void *s, u32 n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; }
static void mzero(void *d, u32 n) { u8 *a = d; while (n--) *a++ = 0; }

/* ---------------- SHA-256 ---------------- */

static const u32 K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(sha256_t *c, const u8 *p)
{
    u32 w[64], a, b, cc, d, e, f, g, h;
    for (int i = 0; i < 16; i++) w[i] = (u32)p[4*i] << 24 | (u32)p[4*i+1] << 16 | (u32)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        u32 s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        u32 s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (int i = 0; i < 64; i++) {
        u32 S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25), ch = (e & f) ^ (~e & g);
        u32 t1 = h + S1 + ch + K256[i] + w[i];
        u32 S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22), mj = (a & b) ^ (a & cc) ^ (b & cc);
        u32 t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

void sha256_init(sha256_t *c)
{
    static const u32 iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    for (int i = 0; i < 8; i++) c->h[i] = iv[i];
    c->len = 0; c->total = 0;
}

void sha256_update(sha256_t *c, const void *d, u32 n)
{
    const u8 *p = d;
    c->total += n;
    while (n) {
        u32 k = 64 - c->len;
        if (k > n) k = n;
        mcopy(c->buf + c->len, p, k);
        c->len += k; p += k; n -= k;
        if (c->len == 64) { sha_block(c, c->buf); c->len = 0; }
    }
}

void sha256_final(sha256_t *c, u8 out[32])
{
    u32 bits_hi = c->total >> 29, bits_lo = c->total << 3;
    u8 pad = 0x80;
    sha256_update(c, &pad, 1);
    pad = 0;
    while (c->len != 56) sha256_update(c, &pad, 1);
    u8 l[8] = { (u8)(bits_hi >> 24), (u8)(bits_hi >> 16), (u8)(bits_hi >> 8), (u8)bits_hi, (u8)(bits_lo >> 24), (u8)(bits_lo >> 16), (u8)(bits_lo >> 8), (u8)bits_lo };
    sha256_update(c, l, 8);
    for (int i = 0; i < 8; i++) { out[4*i] = (u8)(c->h[i] >> 24); out[4*i+1] = (u8)(c->h[i] >> 16); out[4*i+2] = (u8)(c->h[i] >> 8); out[4*i+3] = (u8)c->h[i]; }
}

void sha256(const void *d, u32 n, u8 out[32]) { sha256_t c; sha256_init(&c); sha256_update(&c, d, n); sha256_final(&c, out); }

void hmac_sha256(const u8 *key, u32 kl, const u8 *msg, u32 ml, u8 out[32])
{
    u8 k[64], ip[64], op[64], t[32];
    mzero(k, 64);
    if (kl > 64) sha256(key, kl, k); else mcopy(k, key, kl);
    for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    sha256_t c;
    sha256_init(&c); sha256_update(&c, ip, 64); sha256_update(&c, msg, ml); sha256_final(&c, t);
    sha256_init(&c); sha256_update(&c, op, 64); sha256_update(&c, t, 32); sha256_final(&c, out);
}

void tls12_prf(const u8 *secret, u32 sl, const char *label, const u8 *seed, u32 seedl, u8 *out, u32 olen)
{
    u8 ls[160], a[32], t[32], buf[32 + 160];
    u32 ll = 0;
    while (label[ll]) { ls[ll] = (u8)label[ll]; ll++; }
    mcopy(ls + ll, seed, seedl);
    u32 n = ll + seedl;
    hmac_sha256(secret, sl, ls, n, a);                   /* A(1) */
    for (u32 pos = 0; pos < olen;) {
        mcopy(buf, a, 32); mcopy(buf + 32, ls, n);
        hmac_sha256(secret, sl, buf, 32 + n, t);
        for (u32 i = 0; i < 32 && pos < olen; i++) out[pos++] = t[i];
        hmac_sha256(secret, sl, a, 32, a);               /* A(i+1) */
    }
}

/* ---------------- AES-128 ---------------- */

static u8 sbox[256]; static int sbox_ok;

static void gen_sbox(void)
{
    u8 p = 1, q = 1;
    do {
        p = (u8)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));
        q = (u8)(q ^ (q << 1)); q = (u8)(q ^ (q << 2)); q = (u8)(q ^ (q << 4));
        if (q & 0x80) q ^= 0x09;
        u8 x = (u8)(q ^ (u8)(q << 1 | q >> 7) ^ (u8)(q << 2 | q >> 6) ^ (u8)(q << 3 | q >> 5) ^ (u8)(q << 4 | q >> 4));
        sbox[p] = (u8)(x ^ 0x63);
    } while (p != 1);
    sbox[0] = 0x63;
    sbox_ok = 1;
}

static u8 xt(u8 x) { return (u8)((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }

/* AES по таблице Te0 (1 КБ): подстановка, ShiftRows и MixColumns одним обращением; остальные столбцы - сдвиги слова. */
static u32 te0[256]; static int te_ok;
static void gen_te(void)
{
    if (!sbox_ok) gen_sbox();
    for (int i = 0; i < 256; i++) {
        u32 sb = sbox[i], s2 = xt((u8)sb), s3 = s2 ^ sb;
        te0[i] = s2 << 24 | sb << 16 | sb << 8 | s3;
    }
    te_ok = 1;
}
static inline u32 ror(u32 x, int n) { return x >> n | x << (32 - n); }
static inline u32 ld32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static void aes_enc(const u8 *rk, const u8 in[16], u8 out[16])
{
    if (!te_ok) gen_te();
    u32 s0 = ld32(in) ^ ld32(rk), s1 = ld32(in + 4) ^ ld32(rk + 4), s2 = ld32(in + 8) ^ ld32(rk + 8), s3 = ld32(in + 12) ^ ld32(rk + 12);
    for (int r = 1; r < 10; r++) {
        const u8 *k = rk + 16 * r;
        u32 t0 = te0[s0 >> 24] ^ ror(te0[(s1 >> 16) & 255], 8) ^ ror(te0[(s2 >> 8) & 255], 16) ^ ror(te0[s3 & 255], 24) ^ ld32(k);
        u32 t1 = te0[s1 >> 24] ^ ror(te0[(s2 >> 16) & 255], 8) ^ ror(te0[(s3 >> 8) & 255], 16) ^ ror(te0[s0 & 255], 24) ^ ld32(k + 4);
        u32 t2 = te0[s2 >> 24] ^ ror(te0[(s3 >> 16) & 255], 8) ^ ror(te0[(s0 >> 8) & 255], 16) ^ ror(te0[s1 & 255], 24) ^ ld32(k + 8);
        u32 t3 = te0[s3 >> 24] ^ ror(te0[(s0 >> 16) & 255], 8) ^ ror(te0[(s1 >> 8) & 255], 16) ^ ror(te0[s2 & 255], 24) ^ ld32(k + 12);
        s0 = t0; s1 = t1; s2 = t2; s3 = t3;
    }
    const u8 *k = rk + 160;
    u32 o[4];
    o[0] = ((u32)sbox[s0 >> 24] << 24 | (u32)sbox[(s1 >> 16) & 255] << 16 | (u32)sbox[(s2 >> 8) & 255] << 8 | sbox[s3 & 255]) ^ ld32(k);
    o[1] = ((u32)sbox[s1 >> 24] << 24 | (u32)sbox[(s2 >> 16) & 255] << 16 | (u32)sbox[(s3 >> 8) & 255] << 8 | sbox[s0 & 255]) ^ ld32(k + 4);
    o[2] = ((u32)sbox[s2 >> 24] << 24 | (u32)sbox[(s3 >> 16) & 255] << 16 | (u32)sbox[(s0 >> 8) & 255] << 8 | sbox[s1 & 255]) ^ ld32(k + 8);
    o[3] = ((u32)sbox[s3 >> 24] << 24 | (u32)sbox[(s0 >> 16) & 255] << 16 | (u32)sbox[(s1 >> 8) & 255] << 8 | sbox[s2 & 255]) ^ ld32(k + 12);
    for (int i = 0; i < 4; i++) { out[4*i] = (u8)(o[i] >> 24); out[4*i+1] = (u8)(o[i] >> 16); out[4*i+2] = (u8)(o[i] >> 8); out[4*i+3] = (u8)o[i]; }
}

/* ---------------- GHASH и GCM ---------------- */

static inline void mulx(u32 v[4])                 /* v * x в поле GCM (битовый порядок GCM) */
{
    u32 lsb = v[3] & 1;
    v[3] = v[3] >> 1 | v[2] << 31; v[2] = v[2] >> 1 | v[1] << 31; v[1] = v[1] >> 1 | v[0] << 31; v[0] >>= 1;
    if (lsb) v[0] ^= 0xE1000000u;
}

static u32 ghr[16]; static int ghr_ok;               /* поправка при сдвиге на 4 бита, по выдвинутым битам */

static void ghash_tab(u32 m[16][4], const u8 h[16])
{
    if (!ghr_ok) {
        for (int r = 0; r < 16; r++) { u32 v[4] = { 0, 0, 0, (u32)r }; for (int k = 0; k < 4; k++) mulx(v); ghr[r] = v[0]; }
        ghr_ok = 1;
    }
    u32 v[4];
    for (int i = 0; i < 4; i++) v[i] = ld32(h + 4 * i);
    for (int i = 0; i < 4; i++) { m[0][i] = 0; m[8][i] = v[i]; }
    for (int bit = 4; bit >= 1; bit >>= 1) { mulx(v); for (int i = 0; i < 4; i++) m[bit][i] = v[i]; }
    for (int n = 3; n < 16; n++) {
        if ((n & (n - 1)) == 0) continue;
        int lo = n & -n;
        for (int i = 0; i < 4; i++) m[n][i] = m[lo][i] ^ m[n - lo][i];
    }
}

static void gmul(u32 x[4], const u32 m[16][4])        /* x = x * H, по 4 бита с конца */
{
    u32 z0 = 0, z1 = 0, z2 = 0, z3 = 0;
    for (int i = 15; i >= 0; i--) {
        u32 w = x[i >> 2] >> (8 * (3 - (i & 3)));
        for (int half = 0; half < 2; half++) {
            u32 nib = half == 0 ? w & 15 : (w >> 4) & 15;
            u32 r = z3 & 15;
            z3 = z3 >> 4 | z2 << 28; z2 = z2 >> 4 | z1 << 28; z1 = z1 >> 4 | z0 << 28; z0 = (z0 >> 4) ^ ghr[r];
            z0 ^= m[nib][0]; z1 ^= m[nib][1]; z2 ^= m[nib][2]; z3 ^= m[nib][3];
        }
    }
    x[0] = z0; x[1] = z1; x[2] = z2; x[3] = z3;
}

static void ghash_upd(u32 y[4], const u32 m[16][4], const u8 *d, u32 n)
{
    while (n) {
        u8 b[16];
        u32 k = n > 16 ? 16 : n;
        mzero(b, 16); mcopy(b, d, k);
        for (int i = 0; i < 4; i++) y[i] ^= ld32(b + 4 * i);
        gmul(y, m);
        d += k; n -= k;
    }
}

void aesgcm_init(aesgcm_t *c, const u8 key[16])
{
    if (!sbox_ok) gen_sbox();
    mcopy(c->rk, key, 16);
    u8 rcon = 1;
    for (int i = 16; i < 176; i += 4) {
        u8 t[4] = { c->rk[i-4], c->rk[i-3], c->rk[i-2], c->rk[i-1] };
        if (i % 16 == 0) {
            u8 x = t[0];
            t[0] = (u8)(sbox[t[1]] ^ rcon); t[1] = sbox[t[2]]; t[2] = sbox[t[3]]; t[3] = sbox[x];
            rcon = xt(rcon);
        }
        for (int j = 0; j < 4; j++) c->rk[i + j] = (u8)(c->rk[i - 16 + j] ^ t[j]);
    }
    u8 z[16];
    mzero(z, 16);
    aes_enc(c->rk, z, c->h);
    ghash_tab(c->m, c->h);
}

static void gcm_crypt(const aesgcm_t *c, const u8 nonce[12], const u8 *in, u32 n, u8 *out)
{
    u8 ctr[16], ks[16];
    mcopy(ctr, nonce, 12);
    u32 cnt = 2;
    for (u32 pos = 0; pos < n; pos += 16, cnt++) {
        ctr[12] = (u8)(cnt >> 24); ctr[13] = (u8)(cnt >> 16); ctr[14] = (u8)(cnt >> 8); ctr[15] = (u8)cnt;
        aes_enc(c->rk, ctr, ks);
        u32 k = n - pos > 16 ? 16 : n - pos;
        for (u32 i = 0; i < k; i++) out[pos + i] = (u8)(in[pos + i] ^ ks[i]);
    }
}

static void gcm_tag(const aesgcm_t *c, const u8 nonce[12], const u8 *aad, u32 al, const u8 *ct, u32 n, u8 tag[16])
{
    u32 y[4] = { 0, 0, 0, 0 };
    ghash_upd(y, c->m, aad, al);
    ghash_upd(y, c->m, ct, n);
    u8 l[16];
    mzero(l, 16);
    u32 ab = al << 3, abh = al >> 29, cb = n << 3, cbh = n >> 29;
    l[0] = (u8)(abh >> 24); l[1] = (u8)(abh >> 16); l[2] = (u8)(abh >> 8); l[3] = (u8)abh;
    l[4] = (u8)(ab >> 24); l[5] = (u8)(ab >> 16); l[6] = (u8)(ab >> 8); l[7] = (u8)ab;
    l[8] = (u8)(cbh >> 24); l[9] = (u8)(cbh >> 16); l[10] = (u8)(cbh >> 8); l[11] = (u8)cbh;
    l[12] = (u8)(cb >> 24); l[13] = (u8)(cb >> 16); l[14] = (u8)(cb >> 8); l[15] = (u8)cb;
    ghash_upd(y, c->m, l, 16);
    u8 j0[16], ek[16];
    mcopy(j0, nonce, 12); j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    aes_enc(c->rk, j0, ek);
    for (int i = 0; i < 4; i++) { tag[4*i] = (u8)(y[i] >> 24 ^ ek[4*i]); tag[4*i+1] = (u8)(y[i] >> 16 ^ ek[4*i+1]); tag[4*i+2] = (u8)(y[i] >> 8 ^ ek[4*i+2]); tag[4*i+3] = (u8)(y[i] ^ ek[4*i+3]); }
}

void aesgcm_seal(const aesgcm_t *c, const u8 nonce[12], const u8 *aad, u32 al, const u8 *in, u32 n, u8 *out, u8 tag[16])
{
    gcm_crypt(c, nonce, in, n, out);
    gcm_tag(c, nonce, aad, al, out, n, tag);
}

int aesgcm_open(const aesgcm_t *c, const u8 nonce[12], const u8 *aad, u32 al, const u8 *in, u32 n, u8 *out, const u8 tag[16])
{
    u8 t[16], d = 0;
    gcm_tag(c, nonce, aad, al, in, n, t);
    for (int i = 0; i < 16; i++) d |= (u8)(t[i] ^ tag[i]);
    if (d) return 0;
    gcm_crypt(c, nonce, in, n, out);
    return 1;
}

/* ---------------- X25519 (по TweetNaCl) ---------------- */

typedef i64 gf[16];
static const gf C121665 = { 0xDB41, 1 };

static void car25519(gf o)
{
    for (int i = 0; i < 16; i++) {
        o[i] += (i64)1 << 16;
        i64 c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * 65536;
    }
}

static void sel25519(gf p, gf q, int b)
{
    i64 c = ~((i64)b - 1);
    for (int i = 0; i < 16; i++) { i64 t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; }
}

static void pack25519(u8 *o, const gf n)
{
    gf m, t;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) { m[i] = t[i] - 0xffff - ((m[i-1] >> 16) & 1); m[i-1] &= 0xffff; }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) { o[2*i] = (u8)(t[i] & 0xff); o[2*i+1] = (u8)(t[i] >> 8); }
}

static void unpack25519(gf o, const u8 *n)
{
    for (int i = 0; i < 16; i++) o[i] = n[2*i] + ((i64)n[2*i+1] << 8);
    o[15] &= 0x7fff;
}

static void fA(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void fZ(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }
static void fM(gf o, const gf a, const gf b)
{
    i64 t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++) for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    car25519(o); car25519(o);
}
static void fS(gf o, const gf a) { fM(o, a, a); }

static void inv25519(gf o, const gf i)
{
    gf c;
    for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 253; a >= 0; a--) { fS(c, c); if (a != 2 && a != 4) fM(c, c, i); }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

void x25519(u8 out[32], const u8 scalar[32], const u8 point[32])
{
    u8 z[32];
    i64 x[80];
    gf a, b, c, d, e, f;
    for (int i = 0; i < 31; i++) z[i] = scalar[i];
    z[31] = (u8)((scalar[31] & 127) | 64);
    z[0] &= 248;
    unpack25519(x, point);
    for (int i = 0; i < 16; i++) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) {
        int r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, r); sel25519(c, d, r);
        fA(e, a, c); fZ(a, a, c); fA(c, b, d); fZ(b, b, d);
        fS(d, e); fS(f, a); fM(a, c, a); fM(c, b, e);
        fA(e, a, c); fZ(a, a, c); fS(b, a); fZ(c, d, f);
        fM(a, c, C121665); fA(a, a, d); fM(c, c, a);
        fM(a, d, f); fM(d, b, x); fS(b, e);
        sel25519(a, b, r); sel25519(c, d, r);
    }
    for (int i = 0; i < 16; i++) { x[i + 16] = a[i]; x[i + 32] = c[i]; x[i + 48] = b[i]; x[i + 64] = d[i]; }
    inv25519(x + 32, x + 32);
    fM(x + 16, x + 16, x + 32);
    pack25519(out, x + 16);
}

void x25519_base(u8 out[32], const u8 scalar[32])
{
    u8 base[32];
    mzero(base, 32);
    base[0] = 9;
    x25519(out, scalar, base);
}

/* ---------------- случайные ---------------- */

static u8 rstate[32]; static u32 rcnt, rinit;

void rng_mix(u32 v)
{
    u8 b[36];
    mcopy(b, rstate, 32);
    b[32] = (u8)v; b[33] = (u8)(v >> 8); b[34] = (u8)(v >> 16); b[35] = (u8)(v >> 24);
    sha256(b, 36, rstate);
    rinit++;
}

void rng_fill(u8 *out, u32 n)
{
    while (n) {
        u8 b[40], h[32];
        mcopy(b, rstate, 32);
        b[32] = (u8)rcnt; b[33] = (u8)(rcnt >> 8); b[34] = (u8)(rcnt >> 16); b[35] = (u8)(rcnt >> 24);
        b[36] = 0x5A; b[37] = 0xA5; b[38] = 0x3C; b[39] = 0xC3;
        rcnt++;
        sha256(b, 40, h);
        u32 k = n > 32 ? 32 : n;
        for (u32 i = 0; i < k; i++) out[i] = h[i];
        out += k; n -= k;
        rng_mix(h[31] ^ (u32)h[7] << 8);
    }
}

/* ---------------- WPA2: SHA-1, HMAC-SHA1, PBKDF2, PRF, AES (шифрование/расшифровка блока), key unwrap, CCM ---------------- */

typedef struct { u32 h[5]; u8 buf[64]; u32 len, total; } sha1_t;
static u32 rol32(u32 x, int n) { return x << n | x >> (32 - n); }

static void sha1_block(sha1_t *c, const u8 *p)
{
    u32 w[80];
    for (int i = 0; i < 16; i++) w[i] = (u32)p[4*i] << 24 | (u32)p[4*i+1] << 16 | (u32)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 80; i++) w[i] = rol32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    u32 a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++) {
        u32 f, k;
        if (i < 20) { f = (b & cc) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else { f = b ^ cc ^ d; k = 0xCA62C1D6; }
        u32 t = rol32(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = rol32(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}
static void sha1_init(sha1_t *c) { c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE; c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0; c->len = 0; c->total = 0; }
static void sha1_update(sha1_t *c, const void *d, u32 n)
{
    const u8 *p = d;
    c->total += n;
    while (n) {
        u32 k = 64 - c->len; if (k > n) k = n;
        mcopy(c->buf + c->len, p, k);
        c->len += k; p += k; n -= k;
        if (c->len == 64) { sha1_block(c, c->buf); c->len = 0; }
    }
}
static void sha1_final(sha1_t *c, u8 out[20])
{
    u32 bits_hi = c->total >> 29, bits_lo = c->total << 3;
    u8 pad = 0x80;
    sha1_update(c, &pad, 1);
    pad = 0;
    while (c->len != 56) sha1_update(c, &pad, 1);
    u8 l[8] = { (u8)(bits_hi >> 24), (u8)(bits_hi >> 16), (u8)(bits_hi >> 8), (u8)bits_hi, (u8)(bits_lo >> 24), (u8)(bits_lo >> 16), (u8)(bits_lo >> 8), (u8)bits_lo };
    sha1_update(c, l, 8);
    for (int i = 0; i < 5; i++) { out[4*i] = (u8)(c->h[i] >> 24); out[4*i+1] = (u8)(c->h[i] >> 16); out[4*i+2] = (u8)(c->h[i] >> 8); out[4*i+3] = (u8)c->h[i]; }
}

void hmac_sha1(const u8 *key, u32 kl, const u8 *msg, u32 ml, u8 out[20])
{
    u8 k[64], t[64], in[20];
    mzero(k, 64);
    if (kl > 64) { sha1_t s; sha1_init(&s); sha1_update(&s, key, kl); sha1_final(&s, k); } else mcopy(k, key, kl);
    sha1_t s;
    for (int i = 0; i < 64; i++) t[i] = k[i] ^ 0x36;
    sha1_init(&s); sha1_update(&s, t, 64); sha1_update(&s, msg, ml); sha1_final(&s, in);
    for (int i = 0; i < 64; i++) t[i] = k[i] ^ 0x5c;
    sha1_init(&s); sha1_update(&s, t, 64); sha1_update(&s, in, 20); sha1_final(&s, out);
}

/* PBKDF2-HMAC-SHA1: так из пароля и имени сети получают 32-байтовый ключ PMK (4096 итераций). */
void pbkdf2_sha1(const u8 *pass, u32 pl, const u8 *salt, u32 sl, u32 iter, u8 *out, u32 olen)
{
    u8 s[64 + 4], u[20], t[20];
    if (sl > 64) sl = 64;
    for (u32 blk = 1; olen; blk++) {
        mcopy(s, salt, sl);
        s[sl] = (u8)(blk >> 24); s[sl+1] = (u8)(blk >> 16); s[sl+2] = (u8)(blk >> 8); s[sl+3] = (u8)blk;
        hmac_sha1(pass, pl, s, sl + 4, u);
        mcopy(t, u, 20);
        for (u32 i = 1; i < iter; i++) {
            hmac_sha1(pass, pl, u, 20, u);
            for (int j = 0; j < 20; j++) t[j] ^= u[j];
        }
        u32 k = olen > 20 ? 20 : olen;
        mcopy(out, t, k); out += k; olen -= k;
    }
}

/* PRF из IEEE 802.11i: HMAC-SHA1(key, label || 0 || data || counter). */
void wpa_prf(const u8 *key, u32 kl, const char *label, const u8 *data, u32 dl, u8 *out, u32 olen)
{
    u8 buf[128]; u32 ll = 0;
    while (label[ll]) { buf[ll] = (u8)label[ll]; ll++; }
    buf[ll++] = 0;
    if (dl > 100 - ll) dl = 100 - ll;
    mcopy(buf + ll, data, dl);
    u32 n = ll + dl; u8 ctr = 0;
    while (olen) {
        u8 h[20]; buf[n] = ctr++;
        hmac_sha1(key, kl, buf, n + 1, h);
        u32 k = olen > 20 ? 20 : olen;
        mcopy(out, h, k); out += k; olen -= k;
    }
}

typedef struct { u8 rk[176]; } aes128_t;
void aes128_set_key(aes128_t *c, const u8 key[16])
{
    aesgcm_t g; aesgcm_init(&g, key);
    mcopy(c->rk, g.rk, 176);
}
void aes128_encrypt(const aes128_t *c, const u8 in[16], u8 out[16]) { aes_enc(c->rk, in, out); }

static u8 isbox[256]; static int isbox_ok;
static u8 gmul8(u8 a, u8 b) { u8 r = 0; while (b) { if (b & 1) r ^= a; a = xt(a); b >>= 1; } return r; }
void aes128_decrypt(const aes128_t *c, const u8 in[16], u8 out[16])
{
    if (!sbox_ok) gen_sbox();
    if (!isbox_ok) { for (int i = 0; i < 256; i++) isbox[sbox[i]] = (u8)i; isbox_ok = 1; }
    u8 s[16], t[16];
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ c->rk[160 + i];
    for (int r = 9; r >= 0; r--) {
        for (int col = 0; col < 4; col++)                            /* InvShiftRows + InvSubBytes */
            for (int row = 0; row < 4; row++) t[4 * ((col + row) & 3) + row] = isbox[s[4 * col + row]];
        for (int i = 0; i < 16; i++) t[i] ^= c->rk[16 * r + i];
        if (r > 0) {
            for (int col = 0; col < 4; col++) {
                u8 a0 = t[4*col], a1 = t[4*col+1], a2 = t[4*col+2], a3 = t[4*col+3];
                s[4*col]   = (u8)(gmul8(a0, 14) ^ gmul8(a1, 11) ^ gmul8(a2, 13) ^ gmul8(a3, 9));
                s[4*col+1] = (u8)(gmul8(a0, 9) ^ gmul8(a1, 14) ^ gmul8(a2, 11) ^ gmul8(a3, 13));
                s[4*col+2] = (u8)(gmul8(a0, 13) ^ gmul8(a1, 9) ^ gmul8(a2, 14) ^ gmul8(a3, 11));
                s[4*col+3] = (u8)(gmul8(a0, 11) ^ gmul8(a1, 13) ^ gmul8(a2, 9) ^ gmul8(a3, 14));
            }
        } else mcopy(s, t, 16);
    }
    mcopy(out, s, 16);
}

/* RFC 3394: развернуть ключи группы из EAPOL. in - n*8 байт (n >= 2), out - (n-1)*8 байт. 1 - целостность подтверждена. */
int aes_key_unwrap(const u8 kek[16], const u8 *in, u32 n, u8 *out)
{
    aes128_t k; aes128_set_key(&k, kek);
    u8 a[8], b[16];
    if (n < 2) return 0;
    mcopy(a, in, 8);
    mcopy(out, in + 8, (n - 1) * 8);
    for (int j = 5; j >= 0; j--)
        for (int i = (int)n - 1; i >= 1; i--) {
            u32 t = (u32)((int)(n - 1) * j + i);
            mcopy(b, a, 8); b[7] ^= (u8)t; b[6] ^= (u8)(t >> 8);
            mcopy(b + 8, out + (i - 1) * 8, 8);
            aes128_decrypt(&k, b, b);
            mcopy(a, b, 8); mcopy(out + (i - 1) * 8, b + 8, 8);
        }
    for (int i = 0; i < 8; i++) if (a[i] != 0xA6) return 0;
    return 1;
}

/* AES-CCM для CCMP (802.11i): nonce 13 байт, тег 8 байт. Шифрует/расшифровывает n байт in -> out. */
static void ccm_mac(const aes128_t *k, const u8 nonce[13], const u8 *aad, u32 al, const u8 *msg, u32 n, u8 mac[16])
{
    u8 b[16], x[16];
    b[0] = 0x59; mcopy(b + 1, nonce, 13); b[14] = (u8)(n >> 8); b[15] = (u8)n;
    aes128_encrypt(k, b, x);
    u8 hdr[2] = { (u8)(al >> 8), (u8)al };
    u32 pos = 0; int first = 1;
    while (first || pos < al) {                                       /* AAD с префиксом длины */
        mzero(b, 16); u32 o = 0;
        if (first) { b[0] = hdr[0]; b[1] = hdr[1]; o = 2; first = 0; }
        while (o < 16 && pos < al) b[o++] = aad[pos++];
        for (int i = 0; i < 16; i++) x[i] ^= b[i];
        aes128_encrypt(k, x, x);
    }
    for (u32 p = 0; p < n; p += 16) {
        mzero(b, 16); u32 m = n - p > 16 ? 16 : n - p; mcopy(b, msg + p, m);
        for (int i = 0; i < 16; i++) x[i] ^= b[i];
        aes128_encrypt(k, x, x);
    }
    mcopy(mac, x, 16);
}
static void ccm_ctr(const aes128_t *k, const u8 nonce[13], u32 ctr, u8 out[16])
{
    u8 a[16]; a[0] = 0x01; mcopy(a + 1, nonce, 13); a[14] = (u8)(ctr >> 8); a[15] = (u8)ctr;
    aes128_encrypt(k, a, out);
}
void ccmp_seal(const u8 key[16], const u8 nonce[13], const u8 *aad, u32 al, const u8 *in, u32 n, u8 *out, u8 mic[8])
{
    aes128_t k; aes128_set_key(&k, key);
    u8 mac[16], s[16];
    ccm_mac(&k, nonce, aad, al, in, n, mac);
    ccm_ctr(&k, nonce, 0, s);
    for (int i = 0; i < 8; i++) mic[i] = mac[i] ^ s[i];
    for (u32 p = 0, c = 1; p < n; p += 16, c++) {
        ccm_ctr(&k, nonce, c, s);
        u32 m = n - p > 16 ? 16 : n - p;
        for (u32 i = 0; i < m; i++) out[p + i] = in[p + i] ^ s[i];
    }
}
int ccmp_open(const u8 key[16], const u8 nonce[13], const u8 *aad, u32 al, const u8 *in, u32 n, u8 *out, const u8 mic[8])
{
    aes128_t k; aes128_set_key(&k, key);
    u8 s[16], mac[16], t[8];
    for (u32 p = 0, c = 1; p < n; p += 16, c++) {
        ccm_ctr(&k, nonce, c, s);
        u32 m = n - p > 16 ? 16 : n - p;
        for (u32 i = 0; i < m; i++) out[p + i] = in[p + i] ^ s[i];
    }
    ccm_mac(&k, nonce, aad, al, out, n, mac);
    ccm_ctr(&k, nonce, 0, s);
    int ok = 1;
    for (int i = 0; i < 8; i++) { t[i] = mac[i] ^ s[i]; if (t[i] != mic[i]) ok = 0; }
    return ok;
}
