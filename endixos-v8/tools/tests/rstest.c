extern char *strstr(const char *, const char *);
#include "gui/resolution.c"
extern int printf(const char *, ...);
static u8 disk[200000 * 512];
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
int ahci_init(void) { return 1; }
const ahci_disk_t *ahci_disk(int i) { static ahci_disk_t d = { 0, 200000, "t" }; return i == 0 ? &d : 0; }
int ahci_read(int d, u32 lba, u32 n, void *b) { (void)d; for (u32 i = 0; i < n * 512; i++) ((u8 *)b)[i] = disk[(u64)lba * 512 + i]; return 0; }
int ahci_write(int d, u32 lba, u32 n, const void *b) { (void)d; for (u32 i = 0; i < n * 512; i++) disk[(u64)lba * 512 + i] = ((const u8 *)b)[i]; return 0; }
u32 fb_phys_width(void) { return 800; } u32 fb_width(void) { return 1024; } u32 fb_height(void) { return 600; }
int main(void)
{
    const char *m; static blk_t b; static fat_t f; static char out[4096];
    b.rd = d_rd; b.wr = d_wr; b.ctx = 0; b.sectors = 200000;
    CHECK(res_apply(3, &m) != 0, "без установки отказ");
    res_set_installed(1);
    CHECK(mbr_set_single(&b, 2048, 190000, 0x0C) == 0 && fat_format(&b, 2048, 190000, "T", &f) == 0, "диск готов");
    CHECK(res_apply(3, &m) != 0, "нет grub.cfg - ничего не пишем");
    fat_mkdir(&f, "/boot"); fat_mkdir(&f, "/boot/grub");
    fat_write(&f, "/boot/grub/grub.cfg", "old", 3);
    CHECK(res_apply(3, &m) == 0, "режим сохранён");
    int n = fat_read(&f, "/boot/grub/grub.cfg", out, sizeof out); out[n > 0 ? n : 0] = 0;
    CHECK(n > 100 && strstr(out, "gfxpayload=1280x800x32") && strstr(out, "set default=0"), "grub.cfg: выбранный режим первым");
    CHECK(strstr(out, "1024x768, safe") && strstr(out, "800x600, safe"), "запасные пункты на месте");
    CHECK(res_apply(0, &m) == 0, "wide режим");
    n = fat_read(&f, "/boot/grub/grub.cfg", out, sizeof out); out[n > 0 ? n : 0] = 0;
    CHECK(strstr(out, "endixos.elf wide installed"), "wide передаётся ядру");
    CHECK(res_current() == 0, "текущий режим определён (wide)");
    return fails;
}
