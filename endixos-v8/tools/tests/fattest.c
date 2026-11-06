#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/blk.h"
static FILE *img; static u32 nsec = 200000;
static int rd(void *c, u32 lba, u32 n, void *b) { (void)c; if (lba + n > nsec) return -1; fseek(img, (long)lba * 512, SEEK_SET); return fread(b, 512, n, img) == n ? 0 : -1; }
static int wr(void *c, u32 lba, u32 n, const void *b) { (void)c; if (lba + n > nsec) return -1; fseek(img, (long)lba * 512, SEEK_SET); return fwrite(b, 512, n, img) == n ? 0 : -1; }
int main(int argc, char **argv)
{
    img = fopen(argv[1], "w+b"); if (!img) return 9;
    if (argc > 2) nsec = (u32)atoi(argv[2]);
    static unsigned char z[512]; for (u32 i = 0; i < nsec; i++) fwrite(z, 512, 1, img);
    blk_t b = { rd, wr, 0, nsec };
    if (mbr_set_single(&b, 2048, nsec - 2048, 0x0C)) { puts("mbr fail"); return 1; }
    fat_t f;
    if (fat_format(&b, 2048, nsec - 2048, "ENDIXOS", &f)) { puts("format fail"); return 2; }
    if (fat_mkdir(&f, "/boot") || fat_mkdir(&f, "/boot/grub") || fat_mkdir(&f, "/home") || fat_mkdir(&f, "/home/danil")) { puts("mkdir fail"); return 3; }
    static unsigned char big[1500000]; for (u32 i = 0; i < sizeof big; i++) big[i] = (unsigned char)(i * 7 + (i >> 8));
    if (fat_write(&f, "/boot/endixos.elf", big, sizeof big)) { puts("write big fail"); return 4; }
    const char *cfg = "set timeout=3\nmenuentry \"EndixOS\" { multiboot /boot/endixos.elf }\n";
    if (fat_write(&f, "/boot/grub/grub.cfg", cfg, (u32)strlen(cfg))) { puts("write cfg fail"); return 5; }
    if (fat_write(&f, "/home/danil/users.dat", "hello", 5)) return 6;
    if (fat_write(&f, "/home/danil/users.dat", "replaced longer text", 20)) return 7;
    for (int i = 0; i < 100; i++) { char p[40]; sprintf(p, "/home/f%d.txt", i); if (fat_write(&f, p, p, (u32)strlen(p))) { puts("many fail"); return 8; } }
    static unsigned char back[1500000];
    int n = fat_read(&f, "/boot/endixos.elf", back, sizeof back);
    if (n != (int)sizeof big || memcmp(back, big, sizeof big)) { puts("readback mismatch"); return 10; }
    char t[256]; n = fat_read(&f, "/home/danil/users.dat", t, 63); t[n > 0 ? n : 0] = 0;
    if (strcmp(t, "replaced longer text")) { puts("replace mismatch"); return 11; }
    fat_t g; if (fat_mount(&b, 2048, &g)) return 12;
    if (fat_read(&g, "/boot/grub/grub.cfg", t, 255) != (int)strlen(cfg)) { puts("remount read fail"); return 13; }
    fat_sync(&f); fclose(img); puts("OK"); return 0;
}
