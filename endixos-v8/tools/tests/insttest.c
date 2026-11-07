#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/install.h"
unsigned char blob_boot[512], blob_core[3000];
__asm__(".globl _binary_boot_img_start\n.set _binary_boot_img_start, blob_boot\n.globl _binary_boot_img_end\n.set _binary_boot_img_end, blob_boot+512\n"
        ".globl _binary_core_img_start\n.set _binary_core_img_start, blob_core\n.globl _binary_core_img_end\n.set _binary_core_img_end, blob_core+3000\n");
static FILE *img; static u32 nsec = 300000;
static int rd(void *c, u32 lba, u32 n, void *b) { (void)c; if (lba + n > nsec) return -1; fseek(img, (long)lba * 512, SEEK_SET); return fread(b, 512, n, img) == n ? 0 : -1; }
static int wr(void *c, u32 lba, u32 n, const void *b) { (void)c; if (lba + n > nsec) return -1; fseek(img, (long)lba * 512, SEEK_SET); return fwrite(b, 512, n, img) == n ? 0 : -1; }
static void prog(int p, const char *s) { printf("[%3d%%] %s\n", p, s); }
int main(int argc, char **argv)
{
    (void)argc;
    img = fopen(argv[1], "w+b");
    static unsigned char z[512]; z[446 + 4] = 0x07; for (u32 i = 0; i < nsec; i++) fwrite(z, 512, 1, img);   /* на диске "чужой" раздел: должен быть стёрт */
    blk_t b = { rd, wr, 0, nsec };
    for (int i = 0; i < 512; i++) blob_boot[i] = (unsigned char)(i * 3 + 1);
    for (int i = 0; i < 3000; i++) blob_core[i] = (unsigned char)(i * 5 + 2);
    static unsigned char kern[1500000]; for (u32 i = 0; i < sizeof kern; i++) kern[i] = (unsigned char)(i * 7 + (i >> 8));
    FILE *k = fopen(argv[2], "wb"); fwrite(kern, 1, sizeof kern, k); fclose(k);
    inst_params_t p = { kern, sizeof kern, "set timeout=3\nmenuentry \"EndixOS\" {\n multiboot /boot/endixos.elf wide\n}\n", "danil", "Данил", "secret pass", "netbook" };
    const char *m;
    const char *bad[][4] = { {"", "x", "p", "h"}, {"Danil", "x", "p", "h"}, {"1abc", "x", "p", "h"}, {"verylongname", "x", "p", "h"} };
    for (int i = 0; i < 4; i++) { inst_params_t q = p; q.username = bad[i][0]; q.hostname = bad[i][3]; int r = inst_run(&b, &q, 0, &m); if (r == 0) { puts("bad username accepted!"); return 1; } printf("rejected '%s': %s\n", bad[i][0], m); }
    int r = inst_run(&b, &p, prog, &m);
    if (r) { printf("install failed %d: %s\n", r, m); return 2; }
    char h[100]; unsigned char salt[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}; inst_hash_password("secret pass", salt, h); printf("hash %s\n", h);
    fclose(img); puts("OK"); return 0;
}
