#include "kernel/crypto.h"
extern int printf(const char *, ...); extern int memcmp(const void*,const void*,unsigned long);
void aes128_set_key(void *, const unsigned char *); void aes128_encrypt(const void *, const unsigned char *, unsigned char *); void ccmp_seal(const unsigned char *, const unsigned char *, const unsigned char *, unsigned, const unsigned char *, unsigned, unsigned char *, unsigned char *);
extern void old_aesgcm_init(void *, const unsigned char *);
extern void old_aesgcm_seal(const void *, const unsigned char *, const unsigned char *, unsigned, const unsigned char *, unsigned, unsigned char *, unsigned char *);
extern void old_aes128_set_key(void *, const unsigned char *); extern void old_aes128_encrypt(const void *, const unsigned char *, unsigned char *);
extern void old_ccmp_seal(const unsigned char *, const unsigned char *, const unsigned char *, unsigned, const unsigned char *, unsigned, unsigned char *, unsigned char *);
static unsigned r = 12345; static unsigned char rb(void) { r = r * 1103515245 + 12345; return (unsigned char)(r >> 16); }
int main(void){ int bad = 0;
 for (int t = 0; t < 300; t++) {
  unsigned char key[16], nonce[12], aad[40], in[300], o1[300], o2[300], t1[16], t2[16];
  for (int i = 0; i < 16; i++) key[i] = rb(); for (int i = 0; i < 12; i++) nonce[i] = rb(); for (int i = 0; i < 40; i++) aad[i] = rb(); for (int i = 0; i < 300; i++) in[i] = rb();
  unsigned n = rb() % 290 + (t % 7 == 0 ? 0 : 1), al = rb() % 33;
  aesgcm_t a; static char ob[8192]; aesgcm_init(&a, key); old_aesgcm_init(ob, key);
  aesgcm_seal(&a, nonce, aad, al, in, n, o1, t1); old_aesgcm_seal(ob, nonce, aad, al, in, n, o2, t2);
  if (memcmp(o1, o2, n) || memcmp(t1, t2, 16)) bad++;
  unsigned char d[300]; if (!aesgcm_open(&a, nonce, aad, al, o1, n, d, t1) || memcmp(d, in, n)) bad++;
  static char k1[1024]; static char k2[1024]; aes128_set_key((void*)k1, key); old_aes128_set_key(k2, key);
  unsigned char e1[16], e2[16]; aes128_encrypt((void*)k1, in, e1); old_aes128_encrypt(k2, in, e2); if (memcmp(e1, e2, 16)) bad++;
  unsigned char n13[13]; for (int i = 0; i < 13; i++) n13[i] = rb(); unsigned char m1[8], m2[8];
  ccmp_seal(key, n13, aad, al, in, n, o1, m1); old_ccmp_seal(key, n13, aad, al, in, n, o2, m2); if (memcmp(o1, o2, n) || memcmp(m1, m2, 8)) bad++;
 }
 printf("несовпадений: %d\n", bad); return bad; }
void *old_memcpy(void *d, const void *s, unsigned long n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
