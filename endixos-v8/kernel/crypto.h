#ifndef ENDIX_CRYPTO_H
#define ENDIX_CRYPTO_H

#include "types.h"

/* Криптография для TLS 1.2 (TLS_ECDHE_*_WITH_AES_128_GCM_SHA256): SHA-256, HMAC, AES-128-GCM, X25519, генератор случайных. */

typedef struct { u32 h[8]; u8 buf[64]; u32 len, total; } sha256_t;
void sha256_init(sha256_t *c);
void sha256_update(sha256_t *c, const void *d, u32 n);
void sha256_final(sha256_t *c, u8 out[32]);
void sha256(const void *d, u32 n, u8 out[32]);
void hmac_sha256(const u8 *key, u32 kl, const u8 *msg, u32 ml, u8 out[32]);

/* PRF из TLS 1.2: P_SHA256(secret, label + seed), результат длиной olen. */
void tls12_prf(const u8 *secret, u32 sl, const char *label, const u8 *seed, u32 seedl, u8 *out, u32 olen);

typedef struct { u8 rk[176]; u8 h[16]; } aesgcm_t;
void aesgcm_init(aesgcm_t *c, const u8 key[16]);
/* Шифрует/расшифровывает in -> out (на месте можно), tag 16 байт. nonce 12 байт. */
void aesgcm_seal(const aesgcm_t *c, const u8 nonce[12], const u8 *aad, u32 al, const u8 *in, u32 n, u8 *out, u8 tag[16]);
int  aesgcm_open(const aesgcm_t *c, const u8 nonce[12], const u8 *aad, u32 al, const u8 *in, u32 n, u8 *out, const u8 tag[16]);   /* 1 - тег верный */

void x25519(u8 out[32], const u8 scalar[32], const u8 point[32]);
void x25519_base(u8 out[32], const u8 scalar[32]);

/* Источник случайных: энтропию подмешивают события (мышь, клавиши, время). */
void rng_mix(u32 v);
void rng_fill(u8 *out, u32 n);

#endif
