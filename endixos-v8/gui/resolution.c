#include "resolution.h"
#include "../drivers/rtlwifi.h"
#ifndef RES_TEST_BLOB
__attribute__((weak)) const u8 *rtlwifi_blob(u32 *n) { if (n) *n = 0; return 0; }
#endif
#include "fb.h"
#include "../kernel/blk.h"
#include "../drivers/ahci.h"

typedef struct { const char *name; u32 w, h; int wide; } mode_t_;
static const mode_t_ modes[] = {
    { "1024x600 netbook (via 800x600, undistorted)", 800, 600, 1 },
    { "1024x600 netbook (if BIOS supports it)", 1024, 600, 0 },
    { "1024x768", 1024, 768, 0 },
    { "1280x800", 1280, 800, 0 },
    { "1366x768", 1366, 768, 0 },
    { "1600x900", 1600, 900, 0 },
    { "1920x1080", 1920, 1080, 0 },
};
#define NMODES ((int)(sizeof modes / sizeof modes[0]))
static int installed;

int  res_count(void) { return NMODES; }
const char *res_name(int i) { return i >= 0 && i < NMODES ? modes[i].name : ""; }
void res_set_installed(int inst) { installed = inst; }
int  res_available(void) { return installed; }

int res_current(void)
{
    for (int i = 0; i < NMODES; i++) {
        if (modes[i].wide) { if (fb_phys_width() != fb_width() && fb_height() == modes[i].h) return i; continue; }
        if (fb_phys_width() == fb_width() && fb_width() == modes[i].w && fb_height() == modes[i].h) return i;
    }
    return -1;
}

static char cfg[3600]; static int cn;
static void add(const char *s) { while (*s && cn < (int)sizeof cfg - 1) cfg[cn++] = *s++; cfg[cn] = 0; }
static void num(u32 v) { char t[12]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } while (n) { if (cn < (int)sizeof cfg - 1) cfg[cn++] = t[--n]; } cfg[cn] = 0; }
static void entry_f(const char *name, const mode_t_ *m, const char *file)
{
    add("menuentry \""); add(name); add("\" {\n    set gfxpayload="); num(m->w); add("x"); num(m->h);
    add("x32,1024x768x32\n    multiboot /boot/"); add(file);
    if (m->wide) add(" wide");
    add(" installed\n    module /boot/"); add(file); add(" endixos.elf\n    if [ -f /boot/rtl8192c.bin ]; then module /boot/rtl8192c.bin rtl8192cfw.bin; fi\n    boot\n}\n");
}
static void entry(const char *name, const mode_t_ *m) { entry_f(name, m, "endixos.elf"); }

static int d_rd(void *c, u32 lba, u32 n, void *b) { return ahci_read((int)(long)c, lba, n, b); }
static int d_wr(void *c, u32 lba, u32 n, const void *b) { return ahci_write((int)(long)c, lba, n, b); }

fat_t *res_mount(void)
{
    static blk_t b; static fat_t f; static char probe[64];
    if (!installed) return 0;
    int nd = ahci_init();
    for (int d = 0; d < nd; d++) {
        const ahci_disk_t *dk = ahci_disk(d);
        if (!dk) continue;
        b.rd = d_rd; b.wr = d_wr; b.ctx = (void *)(long)d; b.sectors = dk->sectors;
        u8 type; u32 start, size;
        if (mbr_get_part(&b, 0, &type, &start, &size) != 0 || (type != 0x0B && type != 0x0C)) continue;
        if (fat_mount(&b, start, &f) != 0) continue;
        if (fat_read(&f, "/boot/grub/grub.cfg", probe, sizeof probe) <= 0) continue;     /* только настоящая установка */
        return &f;
    }
    return 0;
}

int res_apply(int i, const char **msg)
{
    static char old[4096];
    *msg = "";
    if (i < 0 || i >= NMODES) { *msg = "Unknown mode."; return -1; }
    if (!installed) { *msg = "Available only on an installed system."; return -2; }
    fat_t *f = res_mount();
    if (!f) { *msg = "System disk not found."; return -4; }
    (void)old;
    cn = 0; cfg[0] = 0;
    add("insmod all_video\ninsmod vbe\ninsmod vga\ninsmod multiboot\nset gfxmode=auto\nset timeout=1\nset timeout_style=hidden\nset default=0\n\n");
    entry(modes[i].name, &modes[i]);
    if (i != 2) entry("EndixOS (1024x768, safe)", &modes[2]);
    static const mode_t_ safe = { "", 800, 600, 0 };
    entry("EndixOS (800x600, safe)", &safe);
    char pk[8];
    if (fat_read(f, "/boot/previous.elf", pk, sizeof pk) > 0) entry_f("EndixOS (previous version)", &modes[i], "previous.elf");   /* копия после обновления */
    if (fat_write(f, "/boot/grub/grub.cfg", cfg, (u32)cn) != 0) { *msg = "Could not write the boot menu."; return -3; }
    *msg = "Saved. Restart EndixOS to apply.";
    return 0;
}

/* Запуск с флешки: положить прошивку Wi-Fi на установленную систему и добавить её в меню загрузки. */
static int has_str(const char *s, int n, const char *w) { int l = 0; while (w[l]) l++; for (int i = 0; i + l <= n; i++) { int k = 0; while (k < l && s[i + k] == w[k]) k++; if (k == l) return 1; } return 0; }
int res_install_fw(const char **msg)
{
    static blk_t b; static fat_t f; static char c[4096], o[4400];
    u32 fl = 0; const u8 *fw = rtlwifi_blob(&fl);
    if (!fw || !fl) { *msg = "No firmware file here. Start from the USB drive made with tools/mkusb.sh."; return -1; }
    int nd = ahci_init();
    for (int d = 0; d < nd; d++) {
        const ahci_disk_t *dk = ahci_disk(d);
        if (!dk) continue;
        b.rd = d_rd; b.wr = d_wr; b.ctx = (void *)(long)d; b.sectors = dk->sectors;
        u8 type; u32 start, size;
        if (mbr_get_part(&b, 0, &type, &start, &size) != 0 || (type != 0x0B && type != 0x0C)) continue;
        if (fat_mount(&b, start, &f) != 0) continue;
        int n = fat_read(&f, "/boot/grub/grub.cfg", c, sizeof c - 1);
        if (n <= 0) continue;
        c[n] = 0;
        if (fat_write(&f, "/boot/rtl8192c.bin", fw, fl) != 0) { *msg = "Could not write the firmware file."; return -2; }
        if (!has_str(c, n, "rtl8192cfw.bin")) {
            int q = 0; const char *ins = "    if [ -f /boot/rtl8192c.bin ]; then module /boot/rtl8192c.bin rtl8192cfw.bin; fi\n";
            for (int i = 0; i < n && q < (int)sizeof o - 120; i++) {
                o[q++] = c[i];
                if (c[i] == '\n' && i >= 12 && has_str(c + (i > 40 ? i - 40 : 0), i > 40 ? 40 : i, " endixos.elf") && has_str(c + (i > 40 ? i - 40 : 0), i > 40 ? 40 : i, "module")) { for (const char *r = ins; *r; r++) o[q++] = *r; }
            }
            if (fat_write(&f, "/boot/grub/grub.cfg", o, (u32)q) != 0) { *msg = "Could not update the boot menu."; return -3; }
        }
        *msg = "Done. Restart EndixOS from the disk, then turn on Wi-Fi in Settings.";
        return 0;
    }
    *msg = "No installed EndixOS found on the disks.";
    return -4;
}
