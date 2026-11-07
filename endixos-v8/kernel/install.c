#include "install.h"
#include "crypto.h"

extern const u8 _binary_boot_img_start[], _binary_boot_img_end[];
extern const u8 _binary_core_img_start[], _binary_core_img_end[];

static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static void cp(void *d, const void *s, u32 n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; }
static void zero(void *p, u32 n) { u8 *a = p; while (n--) *a++ = 0; }

#define PART_START 2048u

int inst_bootloader_available(void)
{
    u32 b = (u32)(_binary_boot_img_end - _binary_boot_img_start), c = (u32)(_binary_core_img_end - _binary_core_img_start);
    return b == 512 && c >= 1024 && c < (PART_START - 1) * 512;
}

int inst_check_params(const inst_params_t *p, const char **err)
{
    u32 n = slen(p->username);
    if (n < 1 || n > 8) { *err = "User name must be 1 to 8 characters."; return -1; }
    for (u32 i = 0; i < n; i++) {
        char c = p->username[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) { *err = "User name: only small letters a-z and digits."; return -2; }
    }
    if (p->username[0] >= '0' && p->username[0] <= '9') { *err = "User name must start with a letter."; return -3; }
    if (slen(p->fullname) > 40) { *err = "Full name is too long."; return -4; }
    if (slen(p->password) > 60) { *err = "Password is too long."; return -5; }
    if (slen(p->hostname) < 1 || slen(p->hostname) > 30) { *err = "Computer name: 1 to 30 characters."; return -6; }
    return 0;
}

static const char hx[] = "0123456789abcdef";
static void hex(char *o, const u8 *d, u32 n) { for (u32 i = 0; i < n; i++) { o[i * 2] = hx[d[i] >> 4]; o[i * 2 + 1] = hx[d[i] & 15]; } }

void inst_hash_password(const char *pw, const u8 salt[16], char out[100])
{
    u8 h[32], buf[16 + 64 + 32];
    u32 pl = slen(pw);
    cp(buf, salt, 16); cp(buf + 16, pw, pl);
    sha256(buf, 16 + pl, h);
    for (int it = 0; it < 5000; it++) {                       /* растягивание: подбор пароля дорожает */
        cp(buf, h, 32); cp(buf + 32, salt, 16); cp(buf + 48, pw, pl);
        sha256(buf, 48 + pl, h);
    }
    hex(out, salt, 16); out[32] = ':'; hex(out + 33, h, 32); out[97] = 0;
}

static int step(inst_progress_t pr, int pct, const char *s) { if (pr) pr(pct, s); return 0; }

int inst_run(blk_t *d, const inst_params_t *p, inst_progress_t pr, const char **msg)
{
    const char *e;
    *msg = "";
    if (inst_check_params(p, &e)) { *msg = e; return -1; }
    if (!inst_bootloader_available()) { *msg = "No bootloader image: build the kernel on a PC with GRUB (grub-mkimage) installed."; return -2; }
    if (d->sectors < PART_START + 140000) { *msg = "The disk is too small (needs at least 100 MB)."; return -3; }
    if (p->kernel_len < 10000 || !p->kernel) { *msg = "The kernel image was not found. Boot from the install USB stick."; return -4; }

    step(pr, 2, "Creating partition table");
    u32 psize = d->sectors - PART_START;
    if (mbr_set_single(d, PART_START, psize, 0x0C)) { *msg = "Cannot write the partition table to the disk."; return -5; }

    step(pr, 8, "Formatting the partition");
    static fat_t f;
    if (fat_format(d, PART_START, psize, "ENDIXOS", &f)) { *msg = "Cannot format the partition."; return -6; }

    step(pr, 40, "Creating folders");
    char path[40];
    if (fat_mkdir(&f, "/boot") || fat_mkdir(&f, "/boot/grub") || fat_mkdir(&f, "/system") || fat_mkdir(&f, "/home")) { *msg = "Cannot create system folders."; return -7; }
    path[0] = 0; cp(path, "/home/", 6); cp(path + 6, p->username, slen(p->username) + 1);
    if (fat_mkdir(&f, path)) { *msg = "Cannot create the home folder."; return -8; }

    step(pr, 45, "Copying the system");
    if (fat_write(&f, "/boot/endixos.elf", p->kernel, p->kernel_len)) { *msg = "Cannot copy the kernel."; return -9; }
    if (fat_write(&f, "/boot/grub/grub.cfg", p->cfg, slen(p->cfg))) { *msg = "Cannot write the boot menu."; return -10; }

    step(pr, 70, "Creating the user");
    static char ub[240];
    u8 salt[16]; char hash[100];
    rng_fill(salt, 16);
    inst_hash_password(p->password, salt, hash);
    u32 n = 0;
    cp(ub + n, p->username, slen(p->username)); n += slen(p->username); ub[n++] = ':';
    cp(ub + n, hash, slen(hash)); n += slen(hash); ub[n++] = ':';
    cp(ub + n, p->fullname, slen(p->fullname)); n += slen(p->fullname); ub[n++] = '\n';
    if (fat_write(&f, "/system/users.dat", ub, n)) { *msg = "Cannot write the user file."; return -11; }
    n = slen(p->hostname);
    cp(ub, p->hostname, n); ub[n++] = '\n';
    if (fat_write(&f, "/system/host.txt", ub, n)) { *msg = "Cannot write the computer name."; return -12; }

    step(pr, 85, "Installing the bootloader");
    u32 clen = (u32)(_binary_core_img_end - _binary_core_img_start), csec = (clen + 511) / 512;
    static u8 core[512 * 2048];
    if (csec > 2047 || csec < 2) { *msg = "The bootloader image has a bad size."; return -13; }
    zero(core, csec * 512); cp(core, _binary_core_img_start, clen);
    /* Список блоков в конце первого сектора core.img: следующий сектор, сколько секторов осталось, сегмент 0x820 (как у grub-setup). */
    u8 *bl = core + 512 - 12;
    bl[0] = 2; bl[1] = bl[2] = bl[3] = bl[4] = bl[5] = bl[6] = bl[7] = 0;
    bl[8] = (u8)(csec - 1); bl[9] = (u8)((csec - 1) >> 8);
    bl[10] = 0x20; bl[11] = 0x08;
    for (u32 off = 0; off < csec; ) {
        u32 c = csec - off > 64 ? 64 : csec - off;
        if (d->wr(d->ctx, 1 + off, c, core + off * 512)) { *msg = "Cannot write the bootloader to the disk."; return -14; }
        off += c;
    }
    u8 boot[512]; cp(boot, _binary_boot_img_start, 512);
    boot[0x5C] = 1; boot[0x5D] = boot[0x5E] = boot[0x5F] = boot[0x60] = boot[0x61] = boot[0x62] = boot[0x63] = 0;   /* core.img начинается с сектора 1 */
    if (mbr_set_code(d, boot)) { *msg = "Cannot write the boot sector."; return -15; }

    step(pr, 100, "Done");
    return 0;
}
