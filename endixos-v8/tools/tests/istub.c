typedef struct _IO_FILE FILE;
extern int fseek(FILE *, long, int); extern unsigned long fread(void *, unsigned long, unsigned long, FILE *); extern unsigned long fwrite(const void *, unsigned long, unsigned long, FILE *);
#include "drivers/ahci.h"
#include "kernel/types.h"
unsigned char blob_boot[512], blob_core[3000];
__asm__(".globl _binary_boot_img_start\n.set _binary_boot_img_start, blob_boot\n.globl _binary_boot_img_end\n.set _binary_boot_img_end, blob_boot+512\n"
        ".globl _binary_core_img_start\n.set _binary_core_img_start, blob_core\n.globl _binary_core_img_end\n.set _binary_core_img_end, blob_core+3000\n");
FILE *istub_img; u32 istub_nsec = 300000; int istub_disks = 1;
static ahci_disk_t D = { 0, 300000, "QEMU HARDDISK TEST" };
int ahci_init(void) { D.sectors = istub_nsec; return istub_disks; }
const char *ahci_status(void) { return "stub"; }
const ahci_disk_t *ahci_disk(int i) { return i < istub_disks ? &D : 0; }
int ahci_read(int d, u32 lba, u32 n, void *b) { (void)d; if (lba + n > istub_nsec) return -1; fseek(istub_img, (long)lba * 512, 0); return fread(b, 512, n, istub_img) == n ? 0 : -1; }
int ahci_write(int d, u32 lba, u32 n, const void *b) { (void)d; if (lba + n > istub_nsec) return -1; fseek(istub_img, (long)lba * 512, 0); return fwrite(b, 512, n, istub_img) == n ? 0 : -1; }
static unsigned char kern[1500000];
const u8 *boot_kernel_image(u32 *len) { for (u32 i = 0; i < sizeof kern; i += 4096) kern[i] = (unsigned char)(i / 4096 + 1); *len = sizeof kern; return kern; }
