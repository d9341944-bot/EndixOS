#include "tls.h"
#include "crypto.h"

#define REC_MAX   (16384 + 2048)
#define HS_MAX    32768

enum { S_IDLE, S_HELLO, S_CERT, S_SKE, S_DONE, S_FIN, S_READY, S_DEAD };

static tls_cb_t cb;
static int st;
static u8 rec[5 + REC_MAX]; static u32 rec_have;
static u8 hs[HS_MAX]; static u32 hs_have;
static sha256_t transcript;
static u8 crandom[32], srandom[32], priv[32], spub[32], master[48];
static u32 suite;
static aesgcm_t wr_k, rd_k;
static u8 wr_iv[4], rd_iv[4];
static u32 wr_seq_hi, wr_seq, rd_seq_hi, rd_seq;
static int wr_enc, rd_enc;
static int got_spub;

static void mcopy(void *d, const void *s, u32 n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; }
static void p16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static u32 g16(const u8 *p) { return (u32)p[0] << 8 | p[1]; }
static u32 g24(const u8 *p) { return (u32)p[0] << 16 | (u32)p[1] << 8 | p[2]; }

static void fail(const char *m)
{
    st = S_DEAD;
    if (cb.closed) cb.closed(m);
}

int tls_active(void) { return st != S_IDLE && st != S_DEAD; }
void tls_reset(void) { st = S_IDLE; rec_have = hs_have = 0; wr_enc = rd_enc = 0; }

/* Отправка одной записи: зашифрованной, если включено шифрование записи. */
static void send_record(u8 type, const u8 *p, u32 n)
{
    u8 out[5 + 8 + 1500 + 16];
    if (n > 1500) return;
    out[0] = type; out[1] = 3; out[2] = 3;
    if (!wr_enc) {
        p16(out + 3, n);
        mcopy(out + 5, p, n);
        cb.send(out, 5 + n);
        return;
    }
    u8 nonce[12], aad[13];
    mcopy(nonce, wr_iv, 4);
    nonce[4] = (u8)(wr_seq_hi >> 24); nonce[5] = (u8)(wr_seq_hi >> 16); nonce[6] = (u8)(wr_seq_hi >> 8); nonce[7] = (u8)wr_seq_hi;
    nonce[8] = (u8)(wr_seq >> 24); nonce[9] = (u8)(wr_seq >> 16); nonce[10] = (u8)(wr_seq >> 8); nonce[11] = (u8)wr_seq;
    mcopy(aad, nonce + 4, 8);
    aad[8] = type; aad[9] = 3; aad[10] = 3; p16(aad + 11, n);
    p16(out + 3, 8 + n + 16);
    mcopy(out + 5, nonce + 4, 8);
    aesgcm_seal(&wr_k, nonce, aad, 13, p, n, out + 13, out + 13 + n);
    wr_seq++;
    cb.send(out, 5 + 8 + n + 16);
}

static void hs_hash(const u8 *p, u32 n) { sha256_update(&transcript, p, n); }

void tls_start(const tls_cb_t *c, const char *sni)
{
    cb = *c;
    tls_reset();
    wr_seq = rd_seq = wr_seq_hi = rd_seq_hi = 0;
    got_spub = 0;
    sha256_init(&transcript);
    rng_fill(crandom, 32);
    rng_fill(priv, 32);

    u8 h[400];
    u32 n = 0, sl = 0;
    while (sni[sl]) sl++;
    h[n++] = 1; h[n++] = 0; h[n++] = 0; h[n++] = 0;            /* тип и длина (допишем) */
    h[n++] = 3; h[n++] = 3;
    mcopy(h + n, crandom, 32); n += 32;
    h[n++] = 0;                                                /* session id */
    p16(h + n, 4); n += 2; p16(h + n, 0xC02F); n += 2; p16(h + n, 0xC02B); n += 2;
    h[n++] = 1; h[n++] = 0;                                    /* compression: null */
    u32 ext_len_at = n; n += 2;
    u32 es = n;
    /* server_name */
    p16(h + n, 0); p16(h + n + 2, sl + 5); p16(h + n + 4, sl + 3); h[n + 6] = 0; p16(h + n + 7, sl); n += 9;
    mcopy(h + n, sni, sl); n += sl;
    /* supported_groups: x25519 */
    /* x25519 первым (им и пользуемся); P-256/P-384 нужны серверам с ECDSA-сертификатом (они проверяют кривую сертификата) */
    p16(h + n, 10); p16(h + n + 2, 8); p16(h + n + 4, 6); p16(h + n + 6, 0x001D); p16(h + n + 8, 0x0017); p16(h + n + 10, 0x0018); n += 12;
    /* ec_point_formats: uncompressed */
    p16(h + n, 11); p16(h + n + 2, 2); h[n + 4] = 1; h[n + 5] = 0; n += 6;
    /* signature_algorithms */
    static const u16 sa[] = { 0x0804, 0x0401, 0x0403, 0x0503, 0x0805, 0x0501, 0x0806, 0x0601 };
    p16(h + n, 13); p16(h + n + 2, 2 + 2 * 8); p16(h + n + 4, 2 * 8); n += 6;
    for (int i = 0; i < 8; i++) { p16(h + n, sa[i]); n += 2; }
    /* renegotiation_info: пусто */
    p16(h + n, 0xFF01); p16(h + n + 2, 1); h[n + 4] = 0; n += 5;
    p16(h + ext_len_at, n - es);
    h[1] = (u8)((n - 4) >> 16); h[2] = (u8)((n - 4) >> 8); h[3] = (u8)(n - 4);
    hs_hash(h, n);
    st = S_HELLO;
    send_record(22, h, n);
}

static void send_flight(void)
{
    u8 shared[32], pub[32];
    x25519_base(pub, priv);
    x25519(shared, priv, spub);
    u8 seed[64], key_block[40];
    mcopy(seed, crandom, 32); mcopy(seed + 32, srandom, 32);
    tls12_prf(shared, 32, "master secret", seed, 64, master, 48);
    mcopy(seed, srandom, 32); mcopy(seed + 32, crandom, 32);
    tls12_prf(master, 48, "key expansion", seed, 64, key_block, 40);
    aesgcm_init(&wr_k, key_block);
    aesgcm_init(&rd_k, key_block + 16);
    mcopy(wr_iv, key_block + 32, 4);
    mcopy(rd_iv, key_block + 36, 4);

    u8 cke[4 + 1 + 32];
    cke[0] = 16; cke[1] = 0; cke[2] = 0; cke[3] = 33; cke[4] = 32;
    mcopy(cke + 5, pub, 32);
    hs_hash(cke, sizeof cke);
    send_record(22, cke, sizeof cke);
    static const u8 ccs[1] = { 1 };
    send_record(20, ccs, 1);
    wr_enc = 1; wr_seq = 0;

    sha256_t t = transcript;
    u8 th[32], fin[16];
    sha256_final(&t, th);
    fin[0] = 20; fin[1] = 0; fin[2] = 0; fin[3] = 12;
    tls12_prf(master, 48, "client finished", th, 32, fin + 4, 12);
    hs_hash(fin, 16);
    send_record(22, fin, 16);
    st = S_FIN;
}

static void on_message(u32 type, const u8 *b, u32 n)
{
    switch (type) {
    case 2: {                                                    /* ServerHello */
        if (st != S_HELLO || n < 38 || g16(b) != 0x0303) { fail("Unsupported TLS version from the server (only TLS 1.2 works)."); return; }
        mcopy(srandom, b + 2, 32);
        u32 sid = b[34];
        if (35 + sid + 3 > n) { fail("Bad ServerHello."); return; }
        suite = g16(b + 35 + sid);
        if (suite != 0xC02F && suite != 0xC02B) { fail("The server chose an unsupported cipher."); return; }
        st = S_CERT;
        break;
    }
    case 11: break;                                              /* Certificate: пока не проверяем */
    case 12:                                                     /* ServerKeyExchange */
        if (n < 4 + 32 || b[0] != 3 || g16(b + 1) != 0x001D || b[3] != 32) { fail("The server uses an unsupported key exchange."); return; }
        mcopy(spub, b + 4, 32);
        got_spub = 1;
        break;
    case 13: fail("The server asks for a client certificate."); return;
    case 14:                                                     /* ServerHelloDone */
        if (!got_spub) { fail("The server sent no key."); return; }
        hs_hash((const u8 *)"", 0);
        break;
    case 20: {                                                   /* Finished сервера */
        if (st != S_FIN || n != 12) { fail("Unexpected Finished."); return; }
        sha256_t t = transcript;
        u8 th[32], v[12], d = 0;
        sha256_final(&t, th);
        tls12_prf(master, 48, "server finished", th, 32, v, 12);
        for (int i = 0; i < 12; i++) d |= (u8)(v[i] ^ b[i]);
        if (d) { fail("Handshake verification failed."); return; }
        st = S_READY;
        if (cb.ready) cb.ready();
        return;
    }
    default: break;
    }
}

static void process_handshake(const u8 *p, u32 n)
{
    if (hs_have + n > HS_MAX) { fail("The certificate chain is too large."); return; }
    mcopy(hs + hs_have, p, n); hs_have += n;
    while (st != S_DEAD && hs_have >= 4) {
        u32 len = g24(hs + 1);
        if (len + 4 > HS_MAX) { fail("The handshake message is too large."); return; }
        if (hs_have < len + 4) break;
        u32 type = hs[0];
        if (type == 20 && rd_enc) { on_message(type, hs + 4, len); if (st == S_DEAD) return; hs_hash(hs, len + 4); }
        else {
            hs_hash(hs, len + 4);
            on_message(type, hs + 4, len);
            if (st == S_DEAD) return;
            if (type == 14 && st != S_DEAD) { send_flight(); }
        }
        u32 used = len + 4;
        for (u32 i = 0; i + used < hs_have; i++) hs[i] = hs[i + used];
        hs_have -= used;
    }
}

static void process_record(void)
{
    u8 type = rec[0];
    u32 n = g16(rec + 3);
    u8 *p = rec + 5;
    if (rd_enc) {
        if (n < 8 + 16) { fail("Bad encrypted record."); return; }
        u8 nonce[12], aad[13];
        u32 pn = n - 8 - 16;
        mcopy(nonce, rd_iv, 4); mcopy(nonce + 4, p, 8);
        aad[0] = (u8)(rd_seq_hi >> 24); aad[1] = (u8)(rd_seq_hi >> 16); aad[2] = (u8)(rd_seq_hi >> 8); aad[3] = (u8)rd_seq_hi;
        aad[4] = (u8)(rd_seq >> 24); aad[5] = (u8)(rd_seq >> 16); aad[6] = (u8)(rd_seq >> 8); aad[7] = (u8)rd_seq;
        aad[8] = type; aad[9] = 3; aad[10] = 3; p16(aad + 11, pn);
        if (!aesgcm_open(&rd_k, nonce, aad, 13, p + 8, pn, p + 8, p + 8 + pn)) { fail("The encrypted data was corrupted (bad record MAC)."); return; }
        rd_seq++;
        p += 8; n = pn;
    }
    switch (type) {
    case 20:
        if (n != 1 || p[0] != 1) { fail("Bad ChangeCipherSpec."); return; }
        rd_enc = 1; rd_seq = 0; rd_seq_hi = 0;
        break;
    case 21:
        if (n >= 2) {
            if (p[1] == 0) { st = S_DEAD; if (cb.closed) cb.closed(0); }
            else if (p[0] == 1) { /* предупреждение */ }
            else { static char m[48] = "The server sent a TLS alert: "; u32 l = 29; u32 v = p[1]; m[l++] = (char)('0' + v / 100 % 10); m[l++] = (char)('0' + v / 10 % 10); m[l++] = (char)('0' + v % 10); m[l] = 0; fail(m); }
        }
        break;
    case 22: process_handshake(p, n); break;
    case 23:
        if (st != S_READY) { fail("Data before the handshake finished."); return; }
        if (cb.data && n) cb.data(p, n);
        break;
    default: break;
    }
}

void tls_input(const u8 *d, u32 n)
{
    while (n && st != S_DEAD && st != S_IDLE) {
        u32 need = rec_have < 5 ? 5 - rec_have : 5 + g16(rec + 3) - rec_have;
        u32 k = n < need ? n : need;
        mcopy(rec + rec_have, d, k);
        rec_have += k; d += k; n -= k;
        if (rec_have == 5 && g16(rec + 3) > REC_MAX) { fail("The server sent an oversized record."); return; }
        if (rec_have >= 5 && rec_have == 5 + g16(rec + 3)) {
            process_record();
            rec_have = 0;
        }
    }
}

void tls_write(const u8 *d, u32 n)
{
    if (st != S_READY) return;
    while (n) {
        u32 k = n > 1100 ? 1100 : n;
        send_record(23, d, k);
        d += k; n -= k;
    }
}
