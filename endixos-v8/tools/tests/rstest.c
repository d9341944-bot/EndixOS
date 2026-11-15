extern char *strstr(const char *, const char *);
#define RES_TEST_BLOB
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
static u8 fwdata[5000];
const u8 *rtlwifi_blob(u32 *n) { if (n) *n = sizeof fwdata; return fwdata; }
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
    int n = (fat_mount(&b, 2048, &f), fat_read(&f, "/boot/grub/grub.cfg", out, sizeof out)); out[n > 0 ? n : 0] = 0;
    CHECK(n > 100 && strstr(out, "gfxpayload=1280x800x32") && strstr(out, "set default=0"), "grub.cfg: выбранный режим первым");
    CHECK(strstr(out, "1024x768, safe") && strstr(out, "800x600, safe"), "запасные пункты на месте");
    CHECK(res_apply(0, &m) == 0, "wide режим");
    n = (fat_mount(&b, 2048, &f), fat_read(&f, "/boot/grub/grub.cfg", out, sizeof out)); out[n > 0 ? n : 0] = 0;
    CHECK(strstr(out, "endixos.elf wide installed"), "wide передаётся ядру");
    CHECK(res_current() == 0, "текущий режим определён (wide)");
    /* прошивка Wi-Fi на установленную систему */
    for (u32 i = 0; i < sizeof fwdata; i++) fwdata[i] = (u8)(i * 7);
    { const char *c0 = "menuentry \"EndixOS\" {\n    multiboot /boot/endixos.elf installed\n    module /boot/endixos.elf endixos.elf\n    boot\n}\nmenuentry \"EndixOS (previous version)\" {\n    multiboot /boot/previous.elf installed\n    module /boot/previous.elf endixos.elf\n    boot\n}\n"; unsigned l0 = 0; while (c0[l0]) l0++; fat_write(&f, "/boot/grub/grub.cfg", c0, l0); }
    CHECK(res_install_fw(&m) == 0, "прошивка установлена");
    { static u8 back[6000]; int k = (fat_mount(&b, 2048, &f), fat_read(&f, "/boot/rtl8192c.bin", back, sizeof back)); CHECK(k == 5000 && back[100] == (u8)(100 * 7), "файл прошивки на диске"); }
    n = (fat_mount(&b, 2048, &f), fat_read(&f, "/boot/grub/grub.cfg", out, sizeof out)); out[n > 0 ? n : 0] = 0;
    { int cnt = 0; for (char *q = out; (q = strstr(q, "module /boot/rtl8192c.bin")); q++) cnt++; CHECK(cnt == 2, "строка с прошивкой добавлена в оба пункта меню"); }
    CHECK(strstr(out, "module /boot/endixos.elf endixos.elf\n    if [") != 0, "строка стоит после ядра");
    n = (fat_mount(&b, 2048, &f), fat_read(&f, "/boot/grub/grub.cfg", out, sizeof out));
    CHECK(res_install_fw(&m) == 0, "повторный запуск безопасен");
    { int n2 = (fat_mount(&b, 2048, &f), fat_read(&f, "/boot/grub/grub.cfg", out, sizeof out)); CHECK(n2 == n, "меню не раздувается"); }
    CHECK(res_apply(3, &m) == 0, "смена режима после этого работает");
    n = (fat_mount(&b, 2048, &f), fat_read(&f, "/boot/grub/grub.cfg", out, sizeof out)); out[n > 0 ? n : 0] = 0;
    CHECK(strstr(out, "module /boot/rtl8192c.bin") != 0, "новые пункты тоже подключают прошивку");
    return fails;
}
