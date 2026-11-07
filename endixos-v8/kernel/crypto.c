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

static void aes_enc(const u8 *rk, const u8 in[16], u8 out[16])
{
    u8 s[16], t[16];
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
    for (int r = 1; r <= 10; r++) {
        for (int c = 0; c < 4; c++)
            for (int row = 0; row < 4; row++) t[4 * c + row] = sbox[s[4 * ((c + row) & 3) + row]];
        if (r < 10) {
            for (int c = 0; c < 4; c++) {
                u8 a0 = t[4*c], a1 = t[4*c+1], a2 = t[4*c+2], a3 = t[4*c+3];
                s[4*c]   = (u8)(xt(a0) ^ (xt(a1) ^ a1) ^ a2 ^ a3);
                s[4*c+1] = (u8)(a0 ^ xt(a1) ^ (xt(a2) ^ a2) ^ a3);
                s[4*c+2] = (u8)(a0 ^ a1 ^ xt(a2) ^ (xt(a3) ^ a3));
                s[4*c+3] = (u8)((xt(a0) ^ a0) ^ a1 ^ a2 ^ xt(a3));
            }
        } else mcopy(s, t, 16);
        for (int i = 0; i < 16; i++) s[i] ^= rk[16 * r + i];
    }
    mcopy(out, s, 16);
}

/* ---------------- GHASH и GCM ---------------- */

static void gmul(u32 x[4], const u32 h[4])
{
    u32 z[4] = { 0, 0, 0, 0 }, v[4] = { h[0], h[1], h[2], h[3] };
    for (int i = 0; i < 128; i++) {
        if ((x[i >> 5] >> (31 - (i & 31))) & 1) { z[0] ^= v[0]; z[1] ^= v[1]; z[2] ^= v[2]; z[3] ^= v[3]; }
        u32 lsb = v[3] & 1;
        v[3] = v[3] >> 1 | v[2] << 31; v[2] = v[2] >> 1 | v[1] << 31; v[1] = v[1] >> 1 | v[0] << 31; v[0] >>= 1;
        if (lsb) v[0] ^= 0xE1000000u;
    }
    x[0] = z[0]; x[1] = z[1]; x[2] = z[2]; x[3] = z[3];
}

static u32 ld32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static void ghash_upd(u32 y[4], const u32 h[4], const u8 *d, u32 n)
{
    while (n) {
        u8 b[16];
        u32 k = n > 16 ? 16 : n;
        mzero(b, 16); mcopy(b, d, k);
        for (int i = 0; i < 4; i++) y[i] ^= ld32(b + 4 * i);
        gmul(y, h);
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
    u32 h[4], y[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 4; i++) h[i] = ld32(c->h + 4 * i);
    ghash_upd(y, h, aad, al);
    ghash_upd(y, h, ct, n);
    u8 l[16];
    mzero(l, 16);
    u32 ab = al << 3, abh = al >> 29, cb = n << 3, cbh = n >> 29;
    l[0] = (u8)(abh >> 24); l[1] = (u8)(abh >> 16); l[2] = (u8)(abh >> 8); l[3] = (u8)abh;
    l[4] = (u8)(ab >> 24); l[5] = (u8)(ab >> 16); l[6] = (u8)(ab >> 8); l[7] = (u8)ab;
    l[8] = (u8)(cbh >> 24); l[9] = (u8)(cbh >> 16); l[10] = (u8)(cbh >> 8); l[11] = (u8)cbh;
    l[12] = (u8)(cb >> 24); l[13] = (u8)(cb >> 16); l[14] = (u8)(cb >> 8); l[15] = (u8)cb;
    ghash_upd(y, h, l, 16);
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
