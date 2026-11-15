#include "kernel/crypto.c"
extern int printf(const char *, ...); extern int memcmp(const void *, const void *, unsigned long);
static void hex(const char *l, const u8 *p, int n) { printf("%s=", l); for (int i = 0; i < n; i++) printf("%02x", p[i]); printf("\n"); }
int main(void)
{
    u8 o[64], o2[64], mic[8];
    hmac_sha1((const u8 *)"key", 3, (const u8 *)"The quick brown fox jumps over the lazy dog", 43, o); hex("hmac", o, 20);
    u8 big[100]; for (int i = 0; i < 100; i++) big[i] = (u8)(i * 7);
    hmac_sha1(big, 100, big, 77, o); hex("hmaclong", o, 20);
    pbkdf2_sha1((const u8 *)"password", 8, (const u8 *)"IEEE", 4, 4096, o, 32); hex("pmk", o, 32);
    wpa_prf(big, 32, "Pairwise key expansion", big, 76, o, 48); hex("prf", o, 48);
    u8 key[16], nonce[13], aad[30], pt[53];
    for (int i = 0; i < 16; i++) key[i] = (u8)(i + 1);
    for (int i = 0; i < 13; i++) nonce[i] = (u8)(0x40 + i);
    for (int i = 0; i < 30; i++) aad[i] = (u8)(0x90 + i);
    for (int i = 0; i < 53; i++) pt[i] = (u8)(i * 3);
    ccmp_seal(key, nonce, aad, 30, pt, 53, o, mic); hex("ccm_ct", o, 53); hex("ccm_mic", mic, 8);
    int ok = ccmp_open(key, nonce, aad, 30, o, 53, o2, mic); printf("open=%d same=%d\n", ok, memcmp(o2, pt, 53) == 0);
    mic[0] ^= 1; printf("tamper=%d\n", ccmp_open(key, nonce, aad, 30, o, 53, o2, mic));
    /* key unwrap: RFC 3394 4.1: KEK 000102..0F, wrapped 1FA68B0A8112B447 AEF34BD8FB5A7B82 9D3E862371D2CFE5 */
    u8 kek[16]; for (int i = 0; i < 16; i++) kek[i] = (u8)i;
    const u8 wr[24] = { 0x1F,0xA6,0x8B,0x0A,0x81,0x12,0xB4,0x47,0xAE,0xF3,0x4B,0xD8,0xFB,0x5A,0x7B,0x82,0x9D,0x3E,0x86,0x23,0x71,0xD2,0xCF,0xE5 };
    printf("unwrap=%d ", aes_key_unwrap(kek, wr, 3, o)); hex("k", o, 16);
    return 0;
}
